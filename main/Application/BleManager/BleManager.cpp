#include "BleManager.h"
#include "BleGattProtocol.h"
#include "CommandManager/CommandManager.h"
#include "SettingsManager/SettingsManager.h"
#include "JsonScope.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include <cstdio>
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
    serviceProvider_.getSettingsManager().Register({ &enableSetting_ });

    if (!enableSetting_.Get())
    {
        ESP_LOGI(TAG, "Initialized (BLE disabled by setting)");
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
        LOCK(s_instance->mutex_);
        s_instance->synced_ = false;
        s_instance->scanning_ = false;
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

    {
        LOCK(mutex_);
        ownAddrType_ = addrType;
        synced_ = true;
    }

    ESP_LOGI(TAG, "Controller synced (own address type %u)", addrType);
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
            int found;
            {
                LOCK(mutex_);
                scanning_ = false;
                found = 0;
                for (const auto& p : peers_) if (p.used) found++;
            }
            ESP_LOGI(TAG, "Scan complete (reason %d), %d gateway(s) heard",
                     event->disc_complete.reason, found);
            return 0;
        }

        default:
            return 0;
    }
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

void BleManager::FormatAddr(const ble_addr_t& addr, char* out, size_t cap)
{
    // On air the address is little-endian; humans read it MSB-first.
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr.val[5], addr.val[4], addr.val[3],
             addr.val[2], addr.val[1], addr.val[0]);
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
    {
        LOCK(mutex_);
        up = stackUp_;
        synced = synced_;
    }

    JsonObject root(out);
    root.field("ok", true);
    root.field("enabled", enableSetting_.Get());
    root.field("stackUp", up);
    root.field("synced", synced);
    root.field("scanning", Scanning());
    root.field("connected", false);      // the connect path lands next
    WritePeers(root);
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
