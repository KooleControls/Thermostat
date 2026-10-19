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

    /// Host the fallback AP on request instead of waiting for the STA attempts
    /// to fail. For someone standing at the unit: it is the way to reach the web
    /// UI when the house network is unusable, or before any credentials exist.
    /// Sticky — nothing retries the configured SSID afterwards until a network
    /// is picked again or the unit reboots.
    void StartAccessPoint();


    bool IsStaConnected() const { return staConnected_; }
    bool IsStaConnecting() const { return staConnecting_; }

    /// The SSID we are on, or trying — empty when none is configured.
    void GetStaSsid(char* out, size_t maxLen) const;

    /// The SSID the fallback AP hosts, and whether it is open (no passphrase) —
    /// the on-screen UI has to tell the user what to join.
    void GetApSsid(char* out, size_t maxLen) const;
    static bool IsApOpen() { return DefaultApPassword[0] == '\0'; }

    /// Associated AP's signal strength in dBm. False when there is none to report
    /// (AP mode, or not associated).
    bool GetRssi(int8_t& out) const { return wifi_interface_.GetRssi(out); }

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
    RequestError Cmd_WifiScan(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "wifi", "scan", &InvokeCommand<&NetworkManager::Cmd_WifiScan> },
    };

    // ── Settings (registered with SettingsManager in Init) ──
    inline static StringSetting wifiSsid_    { "wifi.ssid",     "WiFi SSID",     "" };
    inline static StringSetting wifiPassword_{ "wifi.password", "WiFi Password", "" };
};
