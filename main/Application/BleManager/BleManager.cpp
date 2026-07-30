#include "BleManager.h"
#include "BleGattProtocol.h"
#include "CommandManager/CommandManager.h"
#include "SettingsManager/SettingsManager.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "JsonHelpers.h"
#include "CommandManager/CommandManager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Bond storage lives in the NimBLE "store config" module, which has no public
// header — the IDF examples declare it by hand exactly like this.
extern "C" void ble_store_config_init(void);

// NimBLE's host callbacks (sync/reset) carry no user argument, so the one
// manager instance has to be reachable statically. There is exactly one
// BleManager (owned by ApplicationContext), set in Init() before the stack
// starts, never cleared.
static BleManager* s_instance = nullptr;

BleManager::BleManager(ServiceProvider& serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void BleManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    s_instance = this;
    serviceProvider_.getCommandManager().Register(this, commands_);
    serviceProvider_.getSettingsManager().Register(
        { &enableSetting_, &peerSetting_, &peerTypeSetting_ });

    if (!enableSetting_.Get())
    {
        ESP_LOGI(TAG, "Initialized (BLE disabled by setting)");
        init.SetReady();
        return;
    }

    // Remember the paired gateway, if there is one: the link is re-established
    // without any installer action, here and after every dropout.
    char stored[24] = {};
    peerSetting_.Get(stored, sizeof(stored));
    if (stored[0] != '\0')
    {
        ble_addr_t addr{};
        if (ParseAddr(stored, static_cast<uint8_t>(peerTypeSetting_.Get()), addr))
        {
            target_ = addr;
            haveTarget_ = true;
            ESP_LOGI(TAG, "Paired gateway on record: %s", stored);
        }
        else
        {
            ESP_LOGW(TAG, "Stored peer '%s' is not a valid address, ignoring", stored);
        }
    }

    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = &BleManager::ReconnectTimerCb;
    timerArgs.arg = this;
    timerArgs.name = "ble_reconnect";
    esp_timer_create(&timerArgs, &reconnectTimer_);

    // Transport resources before the stack: the queue must exist before the first
    // notification can arrive. Buffers go to PSRAM — internal DRAM is what the
    // BLE controller and the dispatch task's stack need.
    inQueue_ = xQueueCreate(kInQueueDepth, sizeof(BleChunk));
    writeDone_ = xSemaphoreCreateBinary();
    outFrame_ = static_cast<uint8_t*>(
        heap_caps_malloc(session::HEADER_LEN + BleChunk::MaxLen, MALLOC_CAP_SPIRAM));
    inFrame_ = static_cast<uint8_t*>(
        heap_caps_malloc(session::HEADER_LEN + BleChunk::MaxLen, MALLOC_CAP_SPIRAM));

    if (inQueue_ == nullptr || writeDone_ == nullptr ||
        outFrame_ == nullptr || inFrame_ == nullptr)
    {
        ESP_LOGE(TAG, "Could not allocate the session transport; BLE stays down");
        init.SetReady();
        return;
    }

    dispatchTask_.Init("ble_dispatch", 5, kDispatchStack);
    dispatchTask_.SetHandler([this] { DispatchLoop(); });
    if (!dispatchTask_.Run())
    {
        ESP_LOGE(TAG, "Could not start the BLE dispatch task; BLE stays down");
        init.SetReady();
        return;
    }

    StartStack();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (stack %s)", stackUp_ ? "up" : "DOWN");
}

// ──────────────────────────────────────────────────────────────
// Stack bring-up
// ──────────────────────────────────────────────────────────────

void BleManager::StartStack()
{
    // Pre-flight the internal DRAM. The BLE controller allocates tens of KB of
    // internal RAM and NimBLE's host task needs a 4 KB internal stack; when
    // neither fits, the failure modes are both awful — the controller asserts
    // (BLE assert emi.c 164, which boot-loops the panel) or the host task fails
    // to be created *silently*, because esp_nimble_enable() ignores the result of
    // xTaskCreatePinnedToCore. Refusing up front turns both into one clear line.
    size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "internal DRAM before the stack: %u free, %u largest",
             static_cast<unsigned>(freeInternal), static_cast<unsigned>(largest));
    if (freeInternal < kMinInternalHeap)
    {
        ESP_LOGE(TAG, "BLE not started: only %u bytes of internal DRAM free "
                      "(largest block %u), need at least %u. Free internal RAM "
                      "first — the LVGL draw buffer is the prime suspect.",
                 static_cast<unsigned>(freeInternal), static_cast<unsigned>(largest),
                 static_cast<unsigned>(kMinInternalHeap));
        return;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.sync_cb = &BleManager::OnSyncTrampoline;
    ble_hs_cfg.reset_cb = &BleManager::OnResetTrampoline;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Pairing: the gateway holds a fixed 6-digit passkey (its public install
    // code) and we type it in, so we are the keyboard and it is the display.
    // sm_mitm=1 is what makes the pairing *authenticated* rather than
    // just-works — without it the passkey would not be demanded at all.
    // sm_sc=1 selects LE Secure Connections; bonding persists the keys so a
    // reconnect needs no installer.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();

    // The name we present if a peer ever reads it. We do not advertise.
    ble_svc_gap_init();
    ble_svc_gap_device_name_set("KCThermostat");

    nimble_port_freertos_init(&BleManager::HostTask);
    stackUp_ = true;

    // esp_nimble_enable() creates the host task with xTaskCreatePinnedToCore and
    // ignores the result, so a failure to allocate its 4 KB internal stack is
    // completely silent: no host task, no sync callback, no crash. Log the
    // internal-DRAM figures next to it so that failure mode is diagnosable.
    ESP_LOGI(TAG, "host task requested; internal DRAM left: %u free, %u largest",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

void BleManager::HostTask(void* param)
{
    (void)param;
    ESP_LOGI("BleManager", "NimBLE host task started");
    nimble_port_run();              // returns only on nimble_port_stop()
    nimble_port_freertos_deinit();
}

void BleManager::OnSyncTrampoline()
{
    if (s_instance) s_instance->OnSync();
}

void BleManager::OnResetTrampoline(int reason)
{
    ESP_LOGW("BleManager", "NimBLE host reset, reason %d", reason);
    if (s_instance)
    {
        {
            LOCK(s_instance->mutex_);
            s_instance->synced_ = false;
            s_instance->scanning_ = false;
        }
        s_instance->DropLink("host reset");
    }
}

void BleManager::OnSync()
{
    // Make sure we have an identity address, then remember which type to use
    // for scanning and connecting.
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }

    uint8_t addrType = 0;
    rc = ble_hs_id_infer_auto(0, &addrType);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    bool reconnect;
    {
        LOCK(mutex_);
        ownAddrType_ = addrType;
        synced_ = true;
        reconnect = haveTarget_;
    }

    ESP_LOGI(TAG, "Controller synced (own address type %u)", addrType);

    if (reconnect) StartConnect();
}

// ──────────────────────────────────────────────────────────────
// Scanning
// ──────────────────────────────────────────────────────────────

bool BleManager::StartScan()
{
    uint8_t addrType;
    {
        LOCK(mutex_);
        if (!stackUp_ || !synced_)
        {
            ESP_LOGW(TAG, "Scan requested but stack is not ready");
            return false;
        }
        if (scanning_) return false;

        // A fresh scan starts from an empty list: a gateway that has been
        // switched off should disappear rather than linger from a past scan.
        for (auto& p : peers_) p = Peer{};
        addrType = ownAddrType_;
        scanning_ = true;
    }

    // Active scan: the name lives in the scan response, which only an active
    // scan asks for. Duplicates are NOT filtered — the advertisement and the
    // scan response arrive as separate reports and both are wanted.
    ble_gap_disc_params params = {};
    params.passive = 0;
    params.filter_duplicates = 0;
    params.limited = 0;
    // filter_policy stays 0 (no whitelist) from the zero-init above.

    int rc = ble_gap_disc(addrType, ScanDurationMs, &params,
                          &BleManager::GapEventTrampoline, this);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        LOCK(mutex_);
        scanning_ = false;
        return false;
    }

    ESP_LOGI(TAG, "Scanning for %d ms", static_cast<int>(ScanDurationMs));
    return true;
}

bool BleManager::Scanning() const
{
    LOCK(mutex_);
    return scanning_;
}

int BleManager::GetPeers(Peer* out, int max) const
{
    LOCK(mutex_);
    int n = 0;
    for (const auto& p : peers_)
    {
        if (!p.used || n >= max) continue;
        out[n++] = p;
    }
    return n;
}

// Merge one advertising report into the peer table. The gateway id comes from
// the advertisement's manufacturer data and the name from the scan response, so
// a peer is normally completed by two reports.
void BleManager::NotePeer(const struct ble_gap_disc_desc& disc)
{
    ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) != 0)
        return;

    // Only KC gateways are interesting. The manufacturer data identifies one;
    // a report without it is either the scan response of a peer we already know
    // (merged below) or someone else's device (ignored).
    bool     isGateway = false;
    uint32_t dgid = 0;
    if (fields.mfg_data != nullptr && fields.mfg_data_len >= kcble::MFG_DATA_LEN)
    {
        uint16_t company = static_cast<uint16_t>(fields.mfg_data[0] |
                                                 (fields.mfg_data[1] << 8));
        if (company == kcble::COMPANY_ID &&
            fields.mfg_data[kcble::MFG_OFF_TYPE] == kcble::MFG_TYPE_GATEWAY_V1)
        {
            isGateway = true;
            const uint8_t* d = fields.mfg_data + kcble::MFG_OFF_DGID;
            dgid = static_cast<uint32_t>(d[0]) |
                   (static_cast<uint32_t>(d[1]) << 8) |
                   (static_cast<uint32_t>(d[2]) << 16) |
                   (static_cast<uint32_t>(d[3]) << 24);
        }
    }

    LOCK(mutex_);

    Peer* slot = nullptr;
    for (auto& p : peers_)
        if (p.used && ble_addr_cmp(&p.addr, &disc.addr) == 0) { slot = &p; break; }

    if (slot == nullptr)
    {
        // A scan response for an address we never heard advertise as a gateway
        // is not ours to keep.
        if (!isGateway) return;

        for (auto& p : peers_)
            if (!p.used) { slot = &p; break; }

        if (slot == nullptr)
        {
            ESP_LOGW(TAG, "Peer table full, ignoring further gateways");
            return;
        }
        slot->used = true;
        slot->addr = disc.addr;
    }

    slot->rssi = disc.rssi;
    if (isGateway)
    {
        slot->dgid = dgid;
        slot->hasDgid = true;
    }
    if (fields.name != nullptr && fields.name_len > 0)
    {
        size_t n = fields.name_len;
        if (n > MaxNameLen) n = MaxNameLen;      // ~29 chars is all a scan response holds
        memcpy(slot->name, fields.name, n);
        slot->name[n] = '\0';
    }
}

// ──────────────────────────────────────────────────────────────
// Connect / pair / discover
// ──────────────────────────────────────────────────────────────

bool BleManager::Connect(const ble_addr_t& addr, const char* code)
{
    char text[18];
    FormatAddr(addr, text, sizeof(text));

    {
        LOCK(mutex_);
        if (!stackUp_ || !synced_)
        {
            ESP_LOGW(TAG, "Connect requested but stack is not ready");
            return false;
        }
        target_ = addr;
        haveTarget_ = true;
        // An empty string means "no code given". The code is kept as text up to
        // here because "000000" — the factory default — is a perfectly valid
        // passkey that a numeric 0 cannot be distinguished from.
        havePasskey_ = (code != nullptr && code[0] != '\0');
        pendingPasskey_ = havePasskey_
                        ? static_cast<uint32_t>(strtoul(code, nullptr, 10))
                        : 0;
    }

    // Remember the choice before the attempt: a pairing that fails halfway
    // should still leave us retrying the gateway the installer picked, rather
    // than silently forgetting it.
    peerSetting_.Set(text);
    peerTypeSetting_.Set(addr.type);

    // Scanning and connecting cannot run at the same time.
    if (Scanning())
    {
        ble_gap_disc_cancel();
        LOCK(mutex_);
        scanning_ = false;
    }

    StartConnect();
    return true;
}

void BleManager::StartConnect()
{
    uint8_t    ownType;
    ble_addr_t addr;
    {
        LOCK(mutex_);
        if (!haveTarget_ || !synced_) return;
        if (link_ != LinkState::Down)
        {
            ESP_LOGD(TAG, "Connect skipped, link is %s", StateName(link_));
            return;
        }
        ownType = ownAddrType_;
        addr = target_;
        link_ = LinkState::Connecting;
    }

    char text[18];
    FormatAddr(addr, text, sizeof(text));
    ESP_LOGI(TAG, "Connecting to %s (type %u)", text, addr.type);

    // Ask for a shorter connection interval than NimBLE's 30–50 ms default. As the
    // central we set this at connect time, and it is the dominant cost of a command:
    // a round trip is a notify one way plus a write the other, so it costs a few
    // connection events whatever we do — measured ~193 ms at the default. Both boxes
    // are mains-powered, so the usual battery argument for a long interval does not
    // apply; 15–20 ms is short enough to matter and long enough to leave the ESP32
    // gateway's WiFi coexistence room to breathe.
    ble_gap_conn_params connParams = {};
    connParams.scan_itvl = 16;              // 10 ms, units of 0.625 ms
    connParams.scan_window = 16;
    connParams.itvl_min = 12;               // 15 ms, units of 1.25 ms
    connParams.itvl_max = 16;               // 20 ms
    connParams.latency = 0;                 // never skip a connection event
    connParams.supervision_timeout = 400;   // 4 s, units of 10 ms
    connParams.min_ce_len = 0;
    connParams.max_ce_len = 0;

    int rc = ble_gap_connect(ownType, &addr, ConnectTimeoutMs, &connParams,
                             &BleManager::GapEventTrampoline, this);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_connect failed: %d", rc);
        SetState(LinkState::Down);
        ScheduleReconnect();
    }
}

void BleManager::ScheduleReconnect()
{
    bool want;
    {
        LOCK(mutex_);
        want = haveTarget_ && link_ == LinkState::Down;
    }
    if (!want || reconnectTimer_ == nullptr) return;

    // Never give up on the link (docs/reasoning/2026-07-27-16h17-...): a gateway
    // switched off for a week must be picked up when it returns, with no
    // installer present.
    esp_timer_stop(reconnectTimer_);
    esp_timer_start_once(reconnectTimer_, ReconnectDelayUs);
}

void BleManager::ReconnectTimerCb(void* arg)
{
    auto* self = static_cast<BleManager*>(arg);
    if (self) self->StartConnect();
}

void BleManager::DropLink(const char* why)
{
    uint16_t handle;
    {
        LOCK(mutex_);
        handle = connHandle_;
        connHandle_ = BLE_HS_CONN_HANDLE_NONE;
        link_ = LinkState::Down;
        mtu_ = 23;
        svcStart_ = svcEnd_ = 0;
        inboundValHandle_ = outboundValHandle_ = inboundCccd_ = 0;
    }

    if (handle != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);

    ESP_LOGW(TAG, "Link down: %s", why);
    ScheduleReconnect();
}

void BleManager::Forget()
{
    ble_addr_t addr;
    bool had;
    {
        LOCK(mutex_);
        addr = target_;
        had = haveTarget_;
        haveTarget_ = false;
        havePasskey_ = false;
        pendingPasskey_ = 0;
    }

    peerSetting_.Set("");
    peerTypeSetting_.Set(0);

    if (reconnectTimer_) esp_timer_stop(reconnectTimer_);

    uint16_t handle;
    {
        LOCK(mutex_);
        handle = connHandle_;
        connHandle_ = BLE_HS_CONN_HANDLE_NONE;
        link_ = LinkState::Down;
    }
    if (handle != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);

    // Delete the bond too, otherwise a re-pair with the same gateway would
    // resume the old keys instead of asking for the install code again.
    if (had) ble_store_util_delete_peer(&addr);

    ESP_LOGI(TAG, "Gateway forgotten");
}

void BleManager::SetState(LinkState s)
{
    LOCK(mutex_);
    link_ = s;
}

BleManager::LinkState BleManager::GetLinkState() const
{
    LOCK(mutex_);
    return link_;
}

const char* BleManager::StateName(LinkState s)
{
    switch (s)
    {
        case LinkState::Down:        return "down";
        case LinkState::Connecting:  return "connecting";
        case LinkState::Securing:    return "securing";
        case LinkState::Discovering: return "discovering";
        case LinkState::Ready:       return "ready";
    }
    return "?";
}

// ──────────────────────────────────────────────────────────────
// GAP events
// ──────────────────────────────────────────────────────────────

int BleManager::GapEventTrampoline(struct ble_gap_event* event, void* arg)
{
    auto* self = static_cast<BleManager*>(arg);
    return self ? self->OnGapEvent(event) : 0;
}

int BleManager::OnGapEvent(struct ble_gap_event* event)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_DISC:
            NotePeer(event->disc);
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
        {
            int found = 0;
            {
                LOCK(mutex_);
                scanning_ = false;
                for (const auto& p : peers_) if (p.used) found++;
            }
            ESP_LOGI(TAG, "Scan complete (reason %d), %d gateway(s) heard",
                     event->disc_complete.reason, found);
            return 0;
        }

        case BLE_GAP_EVENT_CONNECT:
        {
            if (event->connect.status != 0)
            {
                ESP_LOGW(TAG, "Connect failed: status %d", event->connect.status);
                SetState(LinkState::Down);
                ScheduleReconnect();
                return 0;
            }

            {
                LOCK(mutex_);
                connHandle_ = event->connect.conn_handle;
                link_ = LinkState::Securing;
            }
            ESP_LOGI(TAG, "Connected (handle %u), starting encryption",
                     event->connect.conn_handle);

            // A bigger MTU is what makes the transfer rate tolerable: one
            // session chunk is one GATT write, so the MTU *is* the chunk size.
            ble_gattc_exchange_mtu(event->connect.conn_handle, nullptr, nullptr);

            // Encrypt/pair BEFORE discovering anything. On a first pairing this
            // triggers the passkey prompt; with a stored bond it silently
            // resumes the old keys.
            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "ble_gap_security_initiate failed: %d", rc);
                DropLink("security could not be started");
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "Disconnected, reason 0x%x", event->disconnect.reason);
            {
                LOCK(mutex_);
                connHandle_ = BLE_HS_CONN_HANDLE_NONE;
                link_ = LinkState::Down;
                mtu_ = 23;
                inboundValHandle_ = outboundValHandle_ = inboundCccd_ = 0;
            }
            ScheduleReconnect();
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
        {
            ble_gap_conn_desc desc{};
            if (event->enc_change.status != 0 ||
                ble_gap_conn_find(event->enc_change.conn_handle, &desc) != 0)
            {
                ESP_LOGW(TAG, "Encryption failed: status %d", event->enc_change.status);
                DropLink("encryption failed");
                return 0;
            }

            // This is the whole authentication decision for this transport, so
            // it is deliberately strict: encrypted is not enough, the link must
            // be *authenticated*, which is only true if the passkey was really
            // used. A just-works pairing gets no command surface.
            if (!desc.sec_state.encrypted || !desc.sec_state.authenticated)
            {
                ESP_LOGE(TAG, "Refusing link: encrypted=%d authenticated=%d",
                         desc.sec_state.encrypted, desc.sec_state.authenticated);
                DropLink("link not authenticated");
                return 0;
            }

            { LOCK(mutex_); havePasskey_ = false; pendingPasskey_ = 0; }   // consumed
            ESP_LOGI(TAG, "Link encrypted and authenticated (bonded=%d)",
                     desc.sec_state.bonded);
            StartDiscovery();
            return 0;
        }

        case BLE_GAP_EVENT_PASSKEY_ACTION:
        {
            if (event->passkey.params.action != BLE_SM_IOACT_INPUT)
            {
                ESP_LOGE(TAG, "Unexpected passkey action %d — the gateway holds "
                              "the passkey and we enter it",
                         event->passkey.params.action);
                return 0;
            }

            uint32_t key = 0;
            bool have = false;
            { LOCK(mutex_); key = pendingPasskey_; have = havePasskey_; }

            if (!have)
            {
                ESP_LOGE(TAG, "Gateway asked for a passkey but none was given "
                              "(bond gone? re-pair with the install code)");
                DropLink("no passkey available");
                return 0;
            }

            ble_sm_io io = {};
            io.action = BLE_SM_IOACT_INPUT;
            io.passkey = key;
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &io);
            ESP_LOGI(TAG, "Passkey submitted (rc %d)", rc);
            return 0;
        }

        case BLE_GAP_EVENT_MTU:
            { LOCK(mutex_); mtu_ = event->mtu.value; }
            ESP_LOGI(TAG, "MTU negotiated: %u", event->mtu.value);
            return 0;

        case BLE_GAP_EVENT_NOTIFY_RX:
            // Runs on the NimBLE host task: copy the chunk out and return
            // immediately. Everything slow happens on the dispatch task.
            EnqueueChunk(event->notify_rx.om);
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
        {
            // The gateway wants to pair again while we still hold a bond for it.
            // Drop ours and let the new pairing proceed, otherwise the link
            // would be stuck forever on stale keys.
            ble_gap_conn_desc desc{};
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
                ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        default:
            return 0;
    }
}

// ──────────────────────────────────────────────────────────────
// GATT discovery chain: service → characteristics → CCCD → subscribe
// ──────────────────────────────────────────────────────────────

void BleManager::StartDiscovery()
{
    uint16_t handle;
    {
        LOCK(mutex_);
        handle = connHandle_;
        link_ = LinkState::Discovering;
        svcStart_ = svcEnd_ = 0;
        inboundValHandle_ = outboundValHandle_ = inboundCccd_ = 0;
    }

    int rc = ble_gattc_disc_svc_by_uuid(handle, &kcble::SERVICE_UUID.u,
                                        &BleManager::SvcDiscTrampoline, this);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Service discovery could not start: %d", rc);
        DropLink("service discovery failed to start");
    }
}

int BleManager::SvcDiscTrampoline(uint16_t, const struct ble_gatt_error* error,
                                  const struct ble_gatt_svc* svc, void* arg)
{
    auto* self = static_cast<BleManager*>(arg);
    return self ? self->OnSvcDisc(error, svc) : 0;
}

int BleManager::OnSvcDisc(const struct ble_gatt_error* error,
                          const struct ble_gatt_svc* svc)
{
    if (error->status == 0 && svc != nullptr)
    {
        LOCK(mutex_);
        svcStart_ = svc->start_handle;
        svcEnd_ = svc->end_handle;
        return 0;
    }

    if (error->status != BLE_HS_EDONE)
    {
        ESP_LOGE(TAG, "Service discovery error: %d", error->status);
        DropLink("service discovery error");
        return 0;
    }

    uint16_t handle, start, end;
    {
        LOCK(mutex_);
        handle = connHandle_;
        start = svcStart_;
        end = svcEnd_;
    }

    if (start == 0)
    {
        // Connected to something that is not a KC gateway, or to one running
        // firmware without the service.
        ESP_LOGE(TAG, "Peer does not expose the KC command service");
        DropLink("service not found");
        return 0;
    }

    ESP_LOGI(TAG, "Service found (handles %u..%u), discovering characteristics",
             start, end);
    // One pass over all characteristics, matching UUIDs ourselves — cheaper than
    // two by-uuid discoveries, and it fails loudly if one is missing.
    int rc = ble_gattc_disc_all_chrs(handle, start, end,
                                     &BleManager::ChrDiscTrampoline, this);
    if (rc != 0) DropLink("characteristic discovery failed to start");
    return 0;
}

int BleManager::ChrDiscTrampoline(uint16_t, const struct ble_gatt_error* error,
                                  const struct ble_gatt_chr* chr, void* arg)
{
    auto* self = static_cast<BleManager*>(arg);
    return self ? self->OnChrDisc(error, chr) : 0;
}

int BleManager::OnChrDisc(const struct ble_gatt_error* error,
                          const struct ble_gatt_chr* chr)
{
    if (error->status == 0 && chr != nullptr)
    {
        LOCK(mutex_);
        if (ble_uuid_cmp(&chr->uuid.u, &kcble::CHR_INBOUND_UUID.u) == 0)
            inboundValHandle_ = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &kcble::CHR_OUTBOUND_UUID.u) == 0)
            outboundValHandle_ = chr->val_handle;
        return 0;
    }

    if (error->status != BLE_HS_EDONE)
    {
        ESP_LOGE(TAG, "Characteristic discovery error: %d", error->status);
        DropLink("characteristic discovery error");
        return 0;
    }

    uint16_t handle, inbound, outbound, end;
    {
        LOCK(mutex_);
        handle = connHandle_;
        inbound = inboundValHandle_;
        outbound = outboundValHandle_;
        end = svcEnd_;
    }

    if (inbound == 0 || outbound == 0)
    {
        ESP_LOGE(TAG, "Service is missing a characteristic (in=%u out=%u)",
                 inbound, outbound);
        DropLink("incomplete service");
        return 0;
    }

    ESP_LOGI(TAG, "Characteristics found (in=%u out=%u), finding the CCCD",
             inbound, outbound);
    // The CCCD is a descriptor of the inbound characteristic; writing it is what
    // actually turns notifications on.
    int rc = ble_gattc_disc_all_dscs(handle, inbound, end,
                                     &BleManager::DscDiscTrampoline, this);
    if (rc != 0) DropLink("descriptor discovery failed to start");
    return 0;
}

int BleManager::DscDiscTrampoline(uint16_t, const struct ble_gatt_error* error,
                                  uint16_t, const struct ble_gatt_dsc* dsc, void* arg)
{
    auto* self = static_cast<BleManager*>(arg);
    return self ? self->OnDscDisc(error, dsc) : 0;
}

int BleManager::OnDscDisc(const struct ble_gatt_error* error,
                          const struct ble_gatt_dsc* dsc)
{
    if (error->status == 0 && dsc != nullptr)
    {
        ble_uuid16_t cccd = BLE_UUID16_INIT(BLE_GATT_DSC_CLT_CFG_UUID16);
        LOCK(mutex_);
        if (inboundCccd_ == 0 && ble_uuid_cmp(&dsc->uuid.u, &cccd.u) == 0)
            inboundCccd_ = dsc->handle;
        return 0;
    }

    if (error->status != BLE_HS_EDONE)
    {
        ESP_LOGE(TAG, "Descriptor discovery error: %d", error->status);
        DropLink("descriptor discovery error");
        return 0;
    }

    uint16_t handle, cccdHandle;
    {
        LOCK(mutex_);
        handle = connHandle_;
        cccdHandle = inboundCccd_;
    }

    if (cccdHandle == 0)
    {
        ESP_LOGE(TAG, "Inbound characteristic has no CCCD — cannot subscribe");
        DropLink("no CCCD");
        return 0;
    }

    uint8_t value[2] = { 0x01, 0x00 };     // notifications on
    int rc = ble_gattc_write_flat(handle, cccdHandle, value, sizeof(value),
                                  &BleManager::SubscribeTrampoline, this);
    if (rc != 0) DropLink("subscribe write failed to start");
    return 0;
}

int BleManager::SubscribeTrampoline(uint16_t, const struct ble_gatt_error* error,
                                    struct ble_gatt_attr*, void* arg)
{
    auto* self = static_cast<BleManager*>(arg);
    return self ? self->OnSubscribed(error) : 0;
}

int BleManager::OnSubscribed(const struct ble_gatt_error* error)
{
    if (error->status != 0)
    {
        ESP_LOGE(TAG, "Subscribe failed: %d", error->status);
        DropLink("subscribe failed");
        return 0;
    }

    uint16_t mtu;
    {
        LOCK(mutex_);
        link_ = LinkState::Ready;
        mtu = mtu_;
    }

    // One session chunk = one GATT write, so the usable payload is the MTU less
    // the 3-byte ATT header and the 3-byte session header.
    size_t chunkPayload = (mtu > 6 ? mtu - 3 - 3 : 0);
    if (chunkPayload > BleChunk::MaxLen - session::HEADER_LEN)
        chunkPayload = BleChunk::MaxLen - session::HEADER_LEN;

    {
        LOCK(mutex_);
        outPayloadCap_ = chunkPayload;
    }

    ESP_LOGI(TAG, "Link READY (mtu %u, chunk payload %u bytes)",
             mtu, static_cast<unsigned>(chunkPayload));
    return 0;
}

// ──────────────────────────────────────────────────────────────
// Session transport
// ──────────────────────────────────────────────────────────────

// NimBLE host task context: no blocking, no allocation, no dispatch.
void BleManager::EnqueueChunk(const struct os_mbuf* om)
{
    if (inQueue_ == nullptr) return;

    BleChunk chunk;
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(om, chunk.data, sizeof(chunk.data), &len) != 0)
    {
        ESP_LOGW(TAG, "inbound chunk did not fit %u bytes, dropped",
                 static_cast<unsigned>(sizeof(chunk.data)));
        return;
    }
    chunk.len = len;
    ESP_LOGD(TAG, "notify rx: %u bytes queued", static_cast<unsigned>(len));

    // Dropping is the honest failure: the gateway is outrunning us and silence
    // would look like a hang. Flag it so the session ends on the hole rather than
    // writing around it — a dropped chunk in a firmware image is invisible until
    // the final hash disagrees, minutes of transfer later.
    if (xQueueSend(inQueue_, &chunk, 0) != pdTRUE)
    {
        inboundDropped_.store(true, std::memory_order_relaxed);
        ESP_LOGW(TAG, "inbound queue full, chunk dropped");
    }
}

void BleManager::DispatchLoop()
{
    BleChunk chunk;
    for (;;)
    {
        if (xQueueReceive(inQueue_, &chunk, portMAX_DELAY) != pdTRUE) continue;
        ESP_LOGD(TAG, "dispatch: chunk of %u bytes", static_cast<unsigned>(chunk.len));
        if (chunk.len < session::HEADER_LEN) continue;

        // A request can arrive before our own bring-up has finished. With a
        // bonded peer NimBLE restores the CCCD subscription the moment the link
        // is encrypted, so the gateway is told "subscribed" and can notify while
        // we are still discovering its characteristics — measured at ~1.5 s
        // before we reached Ready. Hold the chunk for that window instead of
        // dropping it, or the first command after every reconnect is lost.
        for (int waited = 0; waited < kReadyWaitTicks; waited++)
        {
            if (GetLinkState() == LinkState::Ready) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        uint16_t conn, outHandle;
        size_t payloadCap;
        {
            LOCK(mutex_);
            if (link_ != LinkState::Ready)
            {
                ESP_LOGW(TAG, "chunk dropped: link never became ready");
                continue;
            }
            conn = connHandle_;
            outHandle = outboundValHandle_;
            payloadCap = outPayloadCap_;
        }

        uint16_t sid = session::readU16(chunk.data);
        uint8_t  flags = chunk.data[2];

        // Cleared per request: a drop belongs to the stream it happened in, and the
        // chunk that opens this one is already in hand.
        inboundDropped_.store(false, std::memory_order_relaxed);

        // Link and mux live for exactly one request, like the WebSocket's do.
        BleSessionLink link(conn, outHandle, inQueue_, writeDone_, &inboundDropped_);
        SessionMux mux(link, *this, outFrame_, payloadCap,
                       inFrame_, session::HEADER_LEN + BleChunk::MaxLen);
        mux.OnChunk(sid, flags, chunk.data + session::HEADER_LEN,
                    chunk.len - session::HEADER_LEN);

        // Anything still queued is residue, not the next request: the peer runs one
        // request at a time, so a session that ended early — a failed upload, say —
        // leaves its remaining body chunks behind. Left in place, the next loop
        // iteration reads firmware bytes as a session header and invents a command
        // out of them. Dropping them here is what keeps one failure from poisoning
        // the request after it.
        int stale = 0;
        BleChunk discard;
        while (xQueueReceive(inQueue_, &discard, 0) == pdTRUE) stale++;
        if (stale > 0)
            ESP_LOGW(TAG, "discarded %d stale chunk(s) left over from the last request",
                     stale);

        // Report the deepest this task has ever gone, once per new low-water mark:
        // its stack is internal DRAM, the scarcest resource on this board, and
        // sizing it by guesswork is how you either crash or waste 2 KB.
        static size_t worst = SIZE_MAX;
        size_t headroom = uxTaskGetStackHighWaterMark(nullptr);
        if (headroom < worst)
        {
            worst = headroom;
            ESP_LOGI(TAG, "dispatch stack headroom low-water: %u bytes of %u",
                     static_cast<unsigned>(headroom),
                     static_cast<unsigned>(kDispatchStack));
        }
    }
}

// Mirrors WebSocketHandler::OnSessionOpened — the routing lives in the sink, so
// the command layer receives an already-resolved call.
void BleManager::OnSessionOpened(Session& session)
{
    const uint8_t* head = nullptr;
    size_t headLen = 0;
    session.peekRequest(head, headLen);

    char line[128];
    size_t n = headLen < sizeof(line) - 1 ? headLen : sizeof(line) - 1;
    memcpy(line, head, n);
    line[n] = '\0';
    if (char* nl = strchr(line, '\n')) *nl = '\0';

    char type[32] = {};
    ExtractJsonString(line, "type", type, sizeof(type));

    if (type[0] == '\0')
    {
        session.reject("missing type");
        return;
    }

    ESP_LOGI(TAG, "Command over BLE: %s", type);

    if (!serviceProvider_.getCommandManager().Execute(type, session, session))
    {
        session.reject(type);
        return;
    }
    session.finish();
}

// ──────────────────────────────────────────────────────────────
// Address helpers
// ──────────────────────────────────────────────────────────────

void BleManager::FormatAddr(const ble_addr_t& addr, char* out, size_t cap)
{
    // On air the address is little-endian; humans read it MSB-first.
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr.val[5], addr.val[4], addr.val[3],
             addr.val[2], addr.val[1], addr.val[0]);
}

bool BleManager::ParseAddr(const char* text, uint8_t type, ble_addr_t& out)
{
    if (text == nullptr) return false;

    unsigned b[6];
    if (sscanf(text, "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;

    for (int i = 0; i < 6; i++)
    {
        if (b[i] > 0xFF) return false;
        out.val[5 - i] = static_cast<uint8_t>(b[i]);   // text is MSB-first
    }
    out.type = type;
    return true;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

void BleManager::Cmd_BleScan(Stream& in, Stream& out)
{
    (void)in;
    // Kick a scan and answer immediately with whatever is known: scanning takes
    // seconds and the caller (display or web UI) polls. Mirrors wifiScan.
    bool started = StartScan();

    JsonObject root(out);
    root.field("ok", true);
    root.field("started", started);
    root.field("scanning", Scanning());
    WritePeers(root);
}

void BleManager::Cmd_BleStatus(Stream& in, Stream& out)
{
    (void)in;
    bool up, synced;
    LinkState state;
    uint16_t mtu;
    {
        LOCK(mutex_);
        up = stackUp_;
        synced = synced_;
        state = link_;
        mtu = mtu_;
    }

    JsonObject root(out);
    root.field("ok", true);
    root.field("enabled", enableSetting_.Get());
    root.field("stackUp", up);
    root.field("synced", synced);
    root.field("scanning", Scanning());
    root.field("link", StateName(state));
    root.field("connected", state == LinkState::Ready);
    root.field("mtu", static_cast<uint32_t>(mtu));
    char peer[24] = {};
    peerSetting_.Get(peer, sizeof(peer));
    root.field("peer", peer);
    WritePeers(root);
}

void BleManager::Cmd_BleConnect(Stream& in, Stream& out)
{
    JsonReader<192> json(in);

    char addrText[24] = {};
    char codeText[16] = {};
    json.GetString("addr", addrText, sizeof(addrText));
    json.GetString("code", codeText, sizeof(codeText));
    int32_t addrTypeVal = json.GetInt("addrType", 0);

    JsonObject root(out);

    ble_addr_t addr{};
    if (!ParseAddr(addrText, static_cast<uint8_t>(addrTypeVal), addr))
    {
        root.field("ok", false);
        root.field("error", "addr must be aa:bb:cc:dd:ee:ff");
        return;
    }

    // The install code is only needed for a first pairing; a stored bond
    // re-encrypts without it, so an empty code is legal here.
    bool ok = Connect(addr, codeText);
    root.field("ok", ok);
    if (!ok) root.field("error", "BLE stack not ready");
}

void BleManager::Cmd_BleForget(Stream& in, Stream& out)
{
    (void)in;
    Forget();
    JsonObject root(out);
    root.field("ok", true);
}

// Appends the peer array to the caller's already-open root object.
void BleManager::WritePeers(JsonObject& root)
{
    Peer snapshot[MaxPeers];
    int count = GetPeers(snapshot, MaxPeers);

    JsonArray peers = root.array("peers");

    for (int i = 0; i < count; i++)
    {
        char addr[18];
        FormatAddr(snapshot[i].addr, addr, sizeof(addr));

        JsonObject p = peers.object();
        p.field("addr", addr);
        p.field("addrType", static_cast<uint32_t>(snapshot[i].addr.type));
        p.field("name", snapshot[i].name);
        p.field("hasDgid", snapshot[i].hasDgid);
        p.field("dgid", snapshot[i].dgid);
        p.field("rssi", static_cast<int32_t>(snapshot[i].rssi));
    }
}
