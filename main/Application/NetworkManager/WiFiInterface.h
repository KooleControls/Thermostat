#pragma once

#include "NetworkInterface.h"
#include "esp_wifi.h"

class WiFiInterface final : public NetworkInterface {
    static constexpr const char* TAG = "WiFiInterface";

public:
    void Init();
    void SetHostname(const char* hostname);

    void ConnectSta(const char* ssid, const char* password);
    void StartAP(const char* ssid, const char* password, uint8_t channel = 1, uint8_t maxConnections = 4);
    void Stop();

    bool IsAP() const { return isAP_; }

    /// WiFi modem sleep. WIFI_PS_NONE keeps the receiver awake permanently (the
    /// default here — lowest latency, but a continuous draw); MIN_MODEM parks it
    /// between beacons and MAX_MODEM parks it for several, both keeping the
    /// association. Applied immediately and remembered across reconnects, since
    /// esp_wifi_start resets it.
    void SetPowerSave(wifi_ps_type_t mode);
    wifi_ps_type_t GetPowerSave() const { return powerSave_; }

    struct ScanResult {
        char ssid[33];
        int8_t rssi;
        uint8_t channel;
        bool secure;
    };

    /// Scan for WiFi networks. Returns the number of results written.
    int Scan(ScanResult* out, int maxResults);

    const char* getName() const override { return "wifi"; }
    void SetEventHandler(NetworkEventHandler handler) override;

private:
    NetworkEventHandler eventHandler_;
    esp_netif_t* staNetif_ = nullptr;
    esp_netif_t* apNetif_ = nullptr;
    bool isAP_ = false;
    wifi_ps_type_t powerSave_ = WIFI_PS_NONE;

    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    void OnWifiEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);
    void RaiseEvent(NetworkEventType type);
};
