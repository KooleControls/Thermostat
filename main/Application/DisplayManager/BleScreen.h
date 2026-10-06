#pragma once

#include "Screen.h"
#include "ServiceProvider.h"
#include "BleManager/BleManager.h"

// Pairing the thermostat to its gateway, from the unit itself: a status line, the
// gateways we can hear, and a numeric keypad for the gateway's install code.
//
// This screen has to exist on the panel rather than only in the web UI: a field
// unit may have WiFi switched off entirely, and the web UI is served over HTTP —
// unreachable over BLE. See
// docs/reasoning/2026-07-29-10h55-3-wifi-is-a-development-convenience.md.
//
// Two views in one screen for the same reason as WifiScreen: pairing is one task
// with steps, and the Navigator deliberately carries no payload to a second
// screen.
class BleScreen final : public Screen
{
    static constexpr uint32_t kRefreshMs = 500;   // status + scan-result polling

public:
    BleScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;   // fresh status + a fresh scan on every entry

private:
    void ShowListView();
    void ShowCodeView(const BleManager::Peer& peer);
    void RefreshStatus();
    void RebuildPeerList();
    void Connect();

    static void RefreshTimerCb(lv_timer_t* t);
    lv_timer_t* refreshTimer_ = nullptr;
    static void BackCb(lv_event_t* e);
    static void RescanCb(lv_event_t* e);
    static void ForgetCb(lv_event_t* e);
    static void PeerCb(lv_event_t* e);
    static void ConnectCb(lv_event_t* e);
    static void CodeBackCb(lv_event_t* e);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;

    lv_obj_t* statusLabel_ = nullptr;

    lv_obj_t* listView_ = nullptr;
    lv_obj_t* peerList_ = nullptr;
    lv_obj_t* rescanButton_ = nullptr;
    lv_obj_t* forgetButton_ = nullptr;

    lv_obj_t* codeView_ = nullptr;
    lv_obj_t* codePrompt_ = nullptr;
    lv_obj_t* codeInput_ = nullptr;

    // The gateway being paired with — copied out of the scan table on tap, so a
    // later scan cannot change what we pair with mid-flow.
    BleManager::Peer selected_{};

    int  shownCount_ = 0;
    bool listStale_ = true;      // rebuild once a scan finishes
};
