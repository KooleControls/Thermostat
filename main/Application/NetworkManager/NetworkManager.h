#pragma once

#include <atomic>
#include <stdint.h>

#include "WiFiInterface.h"
#include "ServiceProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "TypedSettings.h"
#include "Timer.h"

class Stream;

class NetworkManager {
    static constexpr const char* TAG = "NetworkManager";
    static constexpr int StaConnectTimeoutMs = 10000;
    static constexpr int MaxStaRetries = 3;

    static constexpr const char* DefaultApSsid = "KC Thermostat-AP";
    static constexpr const char* DefaultApPassword = ""; // Open network

public:
    explicit NetworkManager(ServiceProvider& serviceProvider);

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
    NetworkManager(NetworkManager&&) = delete;
    NetworkManager& operator=(NetworkManager&&) = delete;

    void Init();

    WiFiInterface& wifi();
    const WiFiInterface& wifi() const;

    bool IsAccessPoint() const { return wifi_interface_.IsAP(); }

    // ── On-screen UI control surface (calls, not JSON commands) ──
    /// Persist new STA credentials and (re)connect right away. Until this
    /// existed, credentials were only read at boot, so changing networks meant
    /// a reboot. Note that it tears down the fallback AP: the caller is standing
    /// at the unit's own touchscreen, where that is exactly the intent.
    void ConnectToStation(const char* ssid, const char* password);

    bool IsStaConnected() const { return staConnected_; }
    bool IsStaConnecting() const { return staConnecting_; }

    /// The SSID we are on, or trying — empty when none is configured.
    void GetStaSsid(char* out, size_t maxLen) const;

private:
    ServiceProvider& serviceProvider_;

    InitState initState;
    WiFiInterface wifi_interface_;

    // STA connection state
    char staSsid_[33] = {};
    char staPassword_[65] = {};
    std::atomic<int> staRetryCount_{0};
    std::atomic<bool> staConnected_{false};
    std::atomic<bool> staConnecting_{false};

    Timer connectTimer_;

    void HandleNetworkEvent(const NetworkEvent& event);
    void AttemptStaConnect();
    void FallbackToAP();

    // ── WebSocket commands (registered with CommandManager in Init) ──
    void Cmd_WifiScan(Stream& in, Stream& out);

    inline static CommandEntry commands_[] = {
        { "wifiScan", &InvokeCommand<&NetworkManager::Cmd_WifiScan> },
    };

    // ── Settings (registered with SettingsManager in Init) ──
    inline static StringSetting wifiSsid_    { "wifi.ssid",     "WiFi SSID",     "" };
    inline static StringSetting wifiPassword_{ "wifi.password", "WiFi Password", "" };
};
