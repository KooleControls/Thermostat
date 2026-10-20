#pragma once

#include "Screen.h"
#include "ScanBar.h"
#include "ServiceProvider.h"
#include "WifiScanner.h"

// Getting the unit onto a network, from the unit itself: a status line, the list
// of nearby access points, and a passphrase keypad for the one you pick.
//
// This is one screen with two views rather than two screens, because joining a
// network is a single task with steps — and because a second screen would need
// the chosen SSID handed to it, which the Navigator (ScreenId only, no payload)
// deliberately can't carry.
class WifiScreen final : public Screen
{
    static constexpr uint32_t kRefreshMs = 500;   // status + scan-result polling

    // How long the first scan after boot is expected to take, before one has
    // been timed: an active scan dwells up to 300 ms on each of 13 channels
    // (WiFiInterface), plus a margin for switching to APSTA when the AP is up.
    // Generous on purpose — a line that finishes early just snaps to the end,
    // one that runs out first sits full while the screen waits.
    static constexpr uint32_t kFirstScanGuessMs = 13 * 300 + 600;

public:
    WifiScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

    void Init();   // hands the scanner its NetworkManager

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;   // fresh status + a fresh scan on every entry

private:
    void ShowListView();
    void ShowPasswordView(const char* ssid);
    void RefreshStatus();
    void CollectScan();       // Scanning → Done transition rebuilds the list
    void RebuildNetworkList();
    void StartScan();
    void Connect(const char* ssid, const char* password);

    static void RefreshTimerCb(lv_timer_t* t);
    lv_timer_t* refreshTimer_ = nullptr;
    static void BackCb(lv_event_t* e);
    static void RescanCb(lv_event_t* e);
    static void ApCb(lv_event_t* e);
    static void NetworkCb(lv_event_t* e);
    static void ConnectCb(lv_event_t* e);
    static void PasswordBackCb(lv_event_t* e);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    WifiScanner scanner_;

    lv_obj_t* statusLabel_ = nullptr;

    lv_obj_t* listView_ = nullptr;
    lv_obj_t* networkList_ = nullptr;
    lv_obj_t* rescanButton_ = nullptr;
    lv_obj_t* apButton_ = nullptr;

    lv_obj_t* passwordView_ = nullptr;
    lv_obj_t* passwordPrompt_ = nullptr;
    lv_obj_t* passwordInput_ = nullptr;

    // The network being joined — copied out of the scan results on tap, so a
    // later scan overwriting the buffer can't change what we connect to.
    char selectedSsid_[33] = {};
    bool listRebuilt_ = false;   // false while a scan is in flight

    // A WiFi scan takes as long as the channels and the answers on them make
    // it, so the line paces itself on how long the last one took — measured to
    // the poll that noticed it end, so it errs long, which is the safe side.
    ScanBar  scanBar_;
    uint32_t scanStartMs_ = 0;
    uint32_t lastScanMs_ = kFirstScanGuessMs;
};
