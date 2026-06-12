#pragma once

#include "ServiceProvider.h"
#include "rtos.h"
#include "KCThermoBleProtocol.h"
#include "host/ble_gap.h"
#include <functional>

// A gateway found while scanning (or the persisted selection).
struct GatewayInfo
{
    char name[28] = "";
    char addrStr[18] = "";  // "aa:bb:cc:dd:ee:ff", for display
    ble_addr_t addr = {};
    int8_t rssi = 0;
};

// BLE central that links the display to one KC1245 gateway (see
// KCThermoBleProtocol.h — the gateway is the GATT server). Supports scanning
// for nearby gateways so the user can pick one on the config screen; the
// selection is persisted in settings and reconnected automatically.
// Demo link: no bonding/encryption.
class BleManager
{
    static constexpr const char *TAG = "BleManager";
    static constexpr int MaxScanResults = 8;
    static constexpr int ReconnectPeriodMs = 5000;

public:
    explicit BleManager(ServiceProvider &serviceProvider);

    BleManager(const BleManager &) = delete;
    BleManager &operator=(const BleManager &) = delete;

    void Init();

    bool IsConnected() const;   // connected, discovered and subscribed
    bool HasGateway() const;    // a gateway has been selected (persisted)
    void GetGatewayName(char *out, size_t maxLen) const;
    void GetGatewayAddr(char *out, size_t maxLen) const;

    // Scanning, for the config screen. Results accumulate until StopScan().
    void StartScan();
    void StopScan();
    int GetScanResults(GatewayInfo *out, int maxCount) const;

    // Persist the selection and (re)connect to it.
    void SelectGateway(const GatewayInfo &gateway);

    // Display -> gateway
    void SendMeasurement(int16_t roomTempTenths);
    void SendIntent(uint8_t type, int16_t value);

    // Gateway -> display. Called from the NimBLE host task.
    using ControlHandler = std::function<void(const KCThermoControl &control)>;
    void SetControlHandler(ControlHandler handler);

private:
    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable RecursiveMutex mutex_;
    Timer reconnectTimer_;

    ControlHandler controlHandler_;

    // Selected gateway (persisted)
    bool hasTarget_ = false;
    ble_addr_t targetAddr_ = {};
    char targetName_[28] = "";

    // Link state
    uint16_t connHandle_ = 0xFFFF;  // BLE_HS_CONN_HANDLE_NONE
    bool linkReady_ = false;        // discovered and subscribed
    uint8_t ownAddrType_ = 0;
    bool scanning_ = false;
    bool hostSynced_ = false;
    uint8_t intentSeq_ = 0;

    // Service discovery
    uint16_t svcStartHandle_ = 0;
    uint16_t svcEndHandle_ = 0;
    uint16_t measValHandle_ = 0;
    uint16_t intentValHandle_ = 0;
    uint16_t controlValHandle_ = 0;

    // Scan results
    GatewayInfo scanResults_[MaxScanResults];
    int scanResultCount_ = 0;

    void LoadTarget();
    void TryConnect();
    void OnDeviceFound(const struct ble_gap_disc_desc &disc);
    void OnConnected(uint16_t handle);
    void OnDisconnected();
    void OnDiscoveryComplete();
    void OnNotify(uint16_t attrHandle, struct os_mbuf *om);
    void Write(uint16_t valHandle, const void *data, uint16_t len);

    static void HostTaskStatic(void *param);
    static void OnSyncStatic();
    static void OnResetStatic(int reason);
    static int GapEventStatic(struct ble_gap_event *event, void *arg);
    static int DiscSvcStatic(uint16_t connHandle, const struct ble_gatt_error *error,
                             const struct ble_gatt_svc *service, void *arg);
    static int DiscChrStatic(uint16_t connHandle, const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg);

    static BleManager *instance;  // NimBLE sync/reset callbacks carry no arg
};
