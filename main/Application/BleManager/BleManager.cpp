#include "BleManager.h"
#include "SettingsManager/SettingsManager.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include <cstring>
#include <cstdio>

BleManager *BleManager::instance = nullptr;

namespace
{
void AddrToString(const ble_addr_t &addr, char *out, size_t maxLen)
{
    snprintf(out, maxLen, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr.val[5], addr.val[4], addr.val[3],
             addr.val[2], addr.val[1], addr.val[0]);
}

bool AddrFromString(const char *str, uint8_t type, ble_addr_t &out)
{
    unsigned v[6];
    if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x",
               &v[5], &v[4], &v[3], &v[2], &v[1], &v[0]) != 6)
        return false;
    for (int i = 0; i < 6; i++)
        out.val[i] = (uint8_t)v[i];
    out.type = type;
    return true;
}
}

// ──────────────────────────────────────────────────────────────
// Construction & Init
// ──────────────────────────────────────────────────────────────

BleManager::BleManager(ServiceProvider &serviceProvider)
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

    LoadTarget();

    instance = this;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        instance = nullptr;
        return;
    }

    ble_hs_cfg.sync_cb = &BleManager::OnSyncStatic;
    ble_hs_cfg.reset_cb = &BleManager::OnResetStatic;

    nimble_port_freertos_init(&BleManager::HostTaskStatic);

    reconnectTimer_.Init("ble_reconn", pdMS_TO_TICKS(ReconnectPeriodMs), true);
    reconnectTimer_.SetHandler([this]() { TryConnect(); });
    reconnectTimer_.Start();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized%s%s", hasTarget_ ? ", gateway: " : ", no gateway selected",
             hasTarget_ ? targetName_ : "");
}

void BleManager::LoadTarget()
{
    auto &settings = serviceProvider_.getSettingsManager();
    char addrStr[18] = "";
    settings.getString("ble.gwAddr", addrStr, sizeof(addrStr));
    settings.getString("ble.gwName", targetName_, sizeof(targetName_));
    int addrType = settings.getInt("ble.gwAddrType", 0);
    hasTarget_ = addrStr[0] != '\0' &&
                 AddrFromString(addrStr, (uint8_t)addrType, targetAddr_);
}

// ──────────────────────────────────────────────────────────────
// Public state
// ──────────────────────────────────────────────────────────────

bool BleManager::IsConnected() const
{
    LOCK(mutex_);
    return linkReady_;
}

bool BleManager::HasGateway() const
{
    LOCK(mutex_);
    return hasTarget_;
}

void BleManager::GetGatewayName(char *out, size_t maxLen) const
{
    LOCK(mutex_);
    snprintf(out, maxLen, "%s", targetName_);
}

void BleManager::GetGatewayAddr(char *out, size_t maxLen) const
{
    LOCK(mutex_);
    if (hasTarget_)
    {
        char str[18];
        AddrToString(targetAddr_, str, sizeof(str));
        snprintf(out, maxLen, "%s", str);
    }
    else if (maxLen > 0)
        out[0] = '\0';
}

// ──────────────────────────────────────────────────────────────
// Scanning & selection
// ──────────────────────────────────────────────────────────────

void BleManager::StartScan()
{
    RETURN_IF_NOT_READY(initState_);
    {
        LOCK(mutex_);
        scanResultCount_ = 0;
        scanning_ = true;
        if (!hostSynced_)
            return;
    }

    // Active scan so we receive the gateway name from the scan response.
    struct ble_gap_disc_params params = {};
    params.passive = 0;
    params.filter_duplicates = 0;  // keep receiving adv + scan rsp pairs

    int rc = ble_gap_disc(ownAddrType_, BLE_HS_FOREVER, &params,
                          &BleManager::GapEventStatic, this);
    if (rc != 0 && rc != BLE_HS_EALREADY)
        ESP_LOGW(TAG, "ble_gap_disc failed: %d", rc);
}

void BleManager::StopScan()
{
    {
        LOCK(mutex_);
        scanning_ = false;
    }
    ble_gap_disc_cancel();
}

int BleManager::GetScanResults(GatewayInfo *out, int maxCount) const
{
    LOCK(mutex_);
    int n = scanResultCount_ < maxCount ? scanResultCount_ : maxCount;
    for (int i = 0; i < n; i++)
        out[i] = scanResults_[i];
    return n;
}

void BleManager::SelectGateway(const GatewayInfo &gateway)
{
    RETURN_IF_NOT_READY(initState_);

    uint16_t oldConn;
    {
        LOCK(mutex_);
        targetAddr_ = gateway.addr;
        snprintf(targetName_, sizeof(targetName_), "%s", gateway.name);
        hasTarget_ = true;
        oldConn = connHandle_;
    }

    auto &settings = serviceProvider_.getSettingsManager();
    settings.setString("ble.gwAddr", gateway.addrStr);
    settings.setInt("ble.gwAddrType", gateway.addr.type);
    settings.setString("ble.gwName", gateway.name);
    settings.Save();

    ESP_LOGI(TAG, "Gateway selected: %s (%s)", gateway.name, gateway.addrStr);

    StopScan();
    if (oldConn != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(oldConn, BLE_ERR_REM_USER_CONN_TERM);  // reconnect to the new one
    else
        TryConnect();
}

// ──────────────────────────────────────────────────────────────
// Connection management
// ──────────────────────────────────────────────────────────────

void BleManager::TryConnect()
{
    RETURN_IF_NOT_READY(initState_);
    {
        LOCK(mutex_);
        if (!hostSynced_ || !hasTarget_ || scanning_ ||
            connHandle_ != BLE_HS_CONN_HANDLE_NONE)
            return;
    }

    ble_addr_t addr = targetAddr_;
    int rc = ble_gap_connect(ownAddrType_, &addr, ReconnectPeriodMs - 1000, nullptr,
                             &BleManager::GapEventStatic, this);
    if (rc != 0 && rc != BLE_HS_EALREADY && rc != BLE_HS_EBUSY)
        ESP_LOGD(TAG, "ble_gap_connect failed: %d", rc);
}

void BleManager::OnDeviceFound(const struct ble_gap_disc_desc &disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) != 0)
        return;

    bool isKcGateway = false;
    for (int i = 0; i < fields.num_uuids128 && !isKcGateway; i++)
        isKcGateway = (ble_uuid_cmp(&fields.uuids128[i].u, &KC_THERMO_SVC_UUID.u) == 0);

    LOCK(mutex_);
    if (!scanning_)
        return;

    // Find an existing entry for this address (adv packet and scan response
    // arrive as separate events and carry different fields).
    int idx = -1;
    for (int i = 0; i < scanResultCount_; i++)
    {
        if (ble_addr_cmp(&scanResults_[i].addr, &disc.addr) == 0)
        {
            idx = i;
            break;
        }
    }

    if (idx < 0)
    {
        if (!isKcGateway || scanResultCount_ >= MaxScanResults)
            return;  // only track devices that advertised our service
        idx = scanResultCount_++;
        GatewayInfo &e = scanResults_[idx];
        e = GatewayInfo();
        e.addr = disc.addr;
        AddrToString(disc.addr, e.addrStr, sizeof(e.addrStr));
    }

    GatewayInfo &entry = scanResults_[idx];
    entry.rssi = disc.rssi;
    if (fields.name && fields.name_len > 0 && entry.name[0] == '\0')
    {
        int n = fields.name_len < (int)sizeof(entry.name) - 1
                    ? fields.name_len : (int)sizeof(entry.name) - 1;
        memcpy(entry.name, fields.name, n);
        entry.name[n] = '\0';
    }
}

void BleManager::OnConnected(uint16_t handle)
{
    {
        LOCK(mutex_);
        connHandle_ = handle;
        linkReady_ = false;
        svcStartHandle_ = svcEndHandle_ = 0;
        measValHandle_ = intentValHandle_ = controlValHandle_ = 0;
    }

    int rc = ble_gattc_disc_svc_by_uuid(handle, &KC_THERMO_SVC_UUID.u,
                                        &BleManager::DiscSvcStatic, this);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Service discovery start failed: %d", rc);
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void BleManager::OnDisconnected()
{
    LOCK(mutex_);
    connHandle_ = BLE_HS_CONN_HANDLE_NONE;
    linkReady_ = false;
    // reconnectTimer_ takes it from here
}

int BleManager::DiscSvcStatic(uint16_t connHandle, const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *service, void *arg)
{
    BleManager *self = static_cast<BleManager *>(arg);

    if (error->status == 0 && service)
    {
        self->svcStartHandle_ = service->start_handle;
        self->svcEndHandle_ = service->end_handle;
        return 0;
    }

    if (error->status == BLE_HS_EDONE && self->svcStartHandle_ != 0)
    {
        int rc = ble_gattc_disc_all_chrs(connHandle, self->svcStartHandle_, self->svcEndHandle_,
                                         &BleManager::DiscChrStatic, self);
        if (rc == 0)
            return 0;
        ESP_LOGW("BleManager", "Characteristic discovery start failed: %d", rc);
    }
    else
    {
        ESP_LOGW("BleManager", "KC service not found on gateway (status=%d)", error->status);
    }

    ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

int BleManager::DiscChrStatic(uint16_t connHandle, const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr, void *arg)
{
    BleManager *self = static_cast<BleManager *>(arg);

    if (error->status == 0 && chr)
    {
        if (ble_uuid_cmp(&chr->uuid.u, &KC_THERMO_MEAS_UUID.u) == 0)
            self->measValHandle_ = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &KC_THERMO_INTENT_UUID.u) == 0)
            self->intentValHandle_ = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &KC_THERMO_CONTROL_UUID.u) == 0)
            self->controlValHandle_ = chr->val_handle;
        return 0;
    }

    if (error->status == BLE_HS_EDONE &&
        self->measValHandle_ && self->intentValHandle_ && self->controlValHandle_)
    {
        self->OnDiscoveryComplete();
        return 0;
    }

    ESP_LOGW("BleManager", "Characteristic discovery incomplete (status=%d)", error->status);
    ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

void BleManager::OnDiscoveryComplete()
{
    // Subscribe to control notifications. Per the protocol contract the
    // control characteristic has exactly one descriptor (its CCCD), so the
    // CCCD handle is the value handle + 1.
    const uint8_t enableNotify[2] = {0x01, 0x00};
    ble_gattc_write_flat(connHandle_, controlValHandle_ + 1, enableNotify,
                         sizeof(enableNotify), nullptr, nullptr);

    LOCK(mutex_);
    linkReady_ = true;
    ESP_LOGI(TAG, "Gateway link ready (%s)", targetName_);
}

void BleManager::OnNotify(uint16_t attrHandle, struct os_mbuf *om)
{
    if (attrHandle != controlValHandle_)
        return;

    KCThermoControl control;
    if (os_mbuf_copydata(om, 0, sizeof(control), &control) != 0)
        return;

    ControlHandler handler;
    {
        LOCK(mutex_);
        handler = controlHandler_;
    }
    if (handler)
        handler(control);
}

// ──────────────────────────────────────────────────────────────
// Data plane
// ──────────────────────────────────────────────────────────────

void BleManager::SetControlHandler(ControlHandler handler)
{
    LOCK(mutex_);
    controlHandler_ = handler;
}

void BleManager::SendMeasurement(int16_t roomTempTenths)
{
    KCThermoMeasurement pkt = {};
    pkt.roomTempTenths = roomTempTenths;
    Write(measValHandle_, &pkt, sizeof(pkt));
}

void BleManager::SendIntent(uint8_t type, int16_t value)
{
    KCThermoIntent pkt = {};
    {
        LOCK(mutex_);
        pkt.seq = ++intentSeq_;
    }
    pkt.type = type;
    pkt.value = value;
    Write(intentValHandle_, &pkt, sizeof(pkt));
}

void BleManager::Write(uint16_t valHandle, const void *data, uint16_t len)
{
    uint16_t conn;
    {
        LOCK(mutex_);
        if (!linkReady_)
            return;
        conn = connHandle_;
    }

    int rc = ble_gattc_write_no_rsp_flat(conn, valHandle, data, len);
    if (rc != 0)
        ESP_LOGD(TAG, "Write failed: %d", rc);
}

// ──────────────────────────────────────────────────────────────
// NimBLE plumbing
// ──────────────────────────────────────────────────────────────

void BleManager::HostTaskStatic(void *)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleManager::OnSyncStatic()
{
    if (!instance)
        return;
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &instance->ownAddrType_);
    {
        LOCK(instance->mutex_);
        instance->hostSynced_ = true;
    }
    instance->TryConnect();
}

void BleManager::OnResetStatic(int reason)
{
    ESP_LOGW("BleManager", "NimBLE host reset, reason=%d", reason);
}

int BleManager::GapEventStatic(struct ble_gap_event *event, void *arg)
{
    BleManager *self = static_cast<BleManager *>(arg);

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        self->OnDeviceFound(event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI("BleManager", "Connected to gateway");
            self->OnConnected(event->connect.conn_handle);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW("BleManager", "Gateway disconnected (reason=%d)", event->disconnect.reason);
        self->OnDisconnected();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        self->OnNotify(event->notify_rx.attr_handle, event->notify_rx.om);
        return 0;

    default:
        return 0;
    }
}
