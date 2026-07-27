#include "WifiScreen.h"
#include "NetworkManager/NetworkManager.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "WifiScreen";

void WifiScreen::Init()
{
    scanner_.Init(serviceProvider_.getNetworkManager());
}

void WifiScreen::Build(lv_obj_t* root)
{
    lv_obj_t* back = AddHeader(root, "WiFi", LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, BackCb, LV_EVENT_CLICKED, this);

    statusLabel_ = lv_label_create(root);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(statusLabel_, UiTheme::TextDim(), 0);
    lv_obj_set_width(statusLabel_, 480 - 2 * UiTheme::Pad);
    lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(statusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(statusLabel_, LV_ALIGN_TOP_MID, 0, UiTheme::HeaderH);
    lv_label_set_text(statusLabel_, "");

    // ── List view: rescan button + the networks we found ─────────
    listView_ = lv_obj_create(root);
    lv_obj_set_size(listView_, LV_PCT(100), 480 - UiTheme::HeaderH - 32);
    lv_obj_align(listView_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(listView_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listView_, 0, 0);
    lv_obj_set_style_pad_all(listView_, UiTheme::Pad, 0);
    lv_obj_remove_flag(listView_, LV_OBJ_FLAG_SCROLLABLE);

    rescanButton_ = lv_button_create(listView_);
    lv_obj_set_size(rescanButton_, 200, 56);
    lv_obj_align(rescanButton_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(rescanButton_, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_color(rescanButton_, UiTheme::Accent(), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(rescanButton_, 0, 0);
    lv_obj_add_event_cb(rescanButton_, RescanCb, LV_EVENT_CLICKED, this);
    lv_obj_t* rescanLabel = lv_label_create(rescanButton_);
    lv_obj_set_style_text_font(rescanLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(rescanLabel, UiTheme::Text(), 0);
    lv_label_set_text(rescanLabel, LV_SYMBOL_REFRESH "  Scan again");
    lv_obj_center(rescanLabel);

    networkList_ = lv_obj_create(listView_);
    lv_obj_set_size(networkList_, LV_PCT(100), 480 - UiTheme::HeaderH - 32 - 2 * UiTheme::Pad - 64);
    lv_obj_align(networkList_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(networkList_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(networkList_, 0, 0);
    lv_obj_set_style_pad_all(networkList_, 0, 0);
    lv_obj_set_style_pad_row(networkList_, 6, 0);
    lv_obj_set_flex_flow(networkList_, LV_FLEX_FLOW_COLUMN);

    // ── Password view: which network, the passphrase, the keyboard ──
    passwordView_ = lv_obj_create(root);
    lv_obj_set_size(passwordView_, LV_PCT(100), 480 - UiTheme::HeaderH);
    lv_obj_align(passwordView_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(passwordView_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passwordView_, 0, 0);
    lv_obj_set_style_pad_all(passwordView_, 0, 0);
    lv_obj_remove_flag(passwordView_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(passwordView_, LV_OBJ_FLAG_HIDDEN);

    passwordPrompt_ = lv_label_create(passwordView_);
    lv_obj_set_style_text_font(passwordPrompt_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(passwordPrompt_, UiTheme::Text(), 0);
    lv_obj_set_width(passwordPrompt_, 480 - 2 * UiTheme::Pad);
    lv_label_set_long_mode(passwordPrompt_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(passwordPrompt_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(passwordPrompt_, LV_ALIGN_TOP_MID, 0, 4);

    passwordInput_ = lv_textarea_create(passwordView_);
    lv_textarea_set_one_line(passwordInput_, true);
    lv_textarea_set_password_mode(passwordInput_, true);
    lv_textarea_set_placeholder_text(passwordInput_, "Password");
    lv_obj_set_size(passwordInput_, 480 - 2 * UiTheme::Pad, 52);
    lv_obj_align(passwordInput_, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_text_font(passwordInput_, &lv_font_montserrat_20, 0);

    lv_obj_t* connect = lv_button_create(passwordView_);
    lv_obj_set_size(connect, 200, 52);
    lv_obj_align(connect, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_bg_color(connect, UiTheme::Accent(), 0);
    lv_obj_set_style_shadow_width(connect, 0, 0);
    lv_obj_add_event_cb(connect, ConnectCb, LV_EVENT_CLICKED, this);
    lv_obj_t* connectLabel = lv_label_create(connect);
    lv_obj_set_style_text_font(connectLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(connectLabel, UiTheme::Text(), 0);
    lv_label_set_text(connectLabel, "Connect");
    lv_obj_center(connectLabel);

    lv_obj_t* keyboard = lv_keyboard_create(passwordView_);
    lv_obj_set_size(keyboard, LV_PCT(100), 240);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, passwordInput_);
    // The keyboard's own tick is a second way to commit; its ✗ goes back.
    lv_obj_add_event_cb(keyboard, ConnectCb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard, PasswordBackCb, LV_EVENT_CANCEL, this);

    lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void WifiScreen::OnShow()
{
    ShowListView();
    RefreshStatus();
    StartScan();
}

void WifiScreen::ShowListView()
{
    lv_obj_add_flag(passwordView_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(listView_, LV_OBJ_FLAG_HIDDEN);
}

void WifiScreen::ShowPasswordView(const char* ssid)
{
    snprintf(selectedSsid_, sizeof(selectedSsid_), "%s", ssid);
    lv_label_set_text_fmt(passwordPrompt_, "Password for %s", selectedSsid_);
    lv_textarea_set_text(passwordInput_, "");

    lv_obj_add_flag(listView_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(passwordView_, LV_OBJ_FLAG_HIDDEN);
}

void WifiScreen::StartScan()
{
    if (!scanner_.Start()) return;   // one already running
    listRebuilt_ = false;
    lv_obj_clean(networkList_);
    lv_obj_add_state(rescanButton_, LV_STATE_DISABLED);
}

void WifiScreen::CollectScan()
{
    if (scanner_.GetState() == WifiScanner::State::Scanning) return;
    if (listRebuilt_) return;

    listRebuilt_ = true;
    lv_obj_remove_state(rescanButton_, LV_STATE_DISABLED);
    RebuildNetworkList();
}

void WifiScreen::RebuildNetworkList()
{
    lv_obj_clean(networkList_);

    const WiFiInterface::ScanResult* results = scanner_.Results();
    int count = scanner_.Count();

    // Strongest first, and only once per SSID: scanning with show_hidden picks up
    // nameless entries, and a dual-band router shows up twice. Neither is useful
    // to tap. Selection sort over indices — at most 16 entries.
    int order[WifiScanner::MaxResults];
    int shown = 0;
    bool taken[WifiScanner::MaxResults] = {};

    for (int slot = 0; slot < count; slot++)
    {
        int best = -1;
        for (int i = 0; i < count; i++)
        {
            if (taken[i]) continue;
            if (best < 0 || results[i].rssi > results[best].rssi) best = i;
        }
        if (best < 0) break;
        taken[best] = true;

        if (results[best].ssid[0] == '\0') continue;   // hidden network

        bool duplicate = false;
        for (int j = 0; j < shown; j++)
            if (std::strcmp(results[order[j]].ssid, results[best].ssid) == 0) duplicate = true;
        if (duplicate) continue;

        order[shown++] = best;
    }

    if (shown == 0)
    {
        lv_obj_t* empty = lv_label_create(networkList_);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(empty, UiTheme::TextDim(), 0);
        lv_label_set_text(empty, "No networks found");
        return;
    }

    for (int i = 0; i < shown; i++)
    {
        const WiFiInterface::ScanResult& net = results[order[i]];

        lv_obj_t* row = lv_button_create(networkList_);
        lv_obj_set_size(row, LV_PCT(100), 60);
        lv_obj_set_style_bg_color(row, UiTheme::Surface(), 0);
        lv_obj_set_style_bg_color(row, UiTheme::Accent(), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_hor(row, UiTheme::Pad, 0);
        lv_obj_add_event_cb(row, NetworkCb, LV_EVENT_CLICKED, this);
        // The row remembers its slot in the sorted list; the SSID is copied out
        // on tap, before any later scan can overwrite the buffer.
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(order[i])));

        lv_obj_t* name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(name, UiTheme::Text(), 0);
        lv_obj_set_width(name, 300);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(name, net.ssid);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* tail = lv_label_create(row);
        lv_obj_set_style_text_font(tail, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tail, UiTheme::TextDim(), 0);
        // The built-in symbol font has no padlock, and an unlabelled network is
        // the surprising case, so mark the open ones rather than the secured ones.
        lv_label_set_text_fmt(tail, "%s%d dBm", net.secure ? "" : "open   ", net.rssi);
        lv_obj_align(tail, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

void WifiScreen::RefreshStatus()
{
    NetworkManager& net = serviceProvider_.getNetworkManager();
    char ssid[33] = {};
    net.GetStaSsid(ssid, sizeof(ssid));

    if (scanner_.GetState() == WifiScanner::State::Scanning)
    {
        lv_label_set_text(statusLabel_, "Scanning...");
        return;
    }

    if (net.IsStaConnected())
    {
        NetworkStatus status = net.wifi().getStatus();
        char buf[64];
        snprintf(buf, sizeof(buf), "%s " LV_SYMBOL_OK "  " IPSTR, ssid, IP2STR(&status.ipv4.ip));
        lv_label_set_text(statusLabel_, buf);
        lv_obj_set_style_text_color(statusLabel_, UiTheme::Text(), 0);
    }
    else if (net.IsStaConnecting())
    {
        lv_label_set_text_fmt(statusLabel_, "Connecting to %s...", ssid);
        lv_obj_set_style_text_color(statusLabel_, UiTheme::TextDim(), 0);
    }
    else if (net.IsAccessPoint())
    {
        lv_label_set_text(statusLabel_, "Own access point - not on a network");
        lv_obj_set_style_text_color(statusLabel_, UiTheme::TextDim(), 0);
    }
    else
    {
        lv_label_set_text(statusLabel_, "Not connected");
        lv_obj_set_style_text_color(statusLabel_, UiTheme::TextDim(), 0);
    }
}

void WifiScreen::Connect(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "Connecting to '%s' from the display", ssid);
    // Back to the list: the status line is the progress indicator from here on.
    ShowListView();
    lv_label_set_text_fmt(statusLabel_, "Connecting to %s...", ssid);

    // ConnectToStation blocks this task for a moment (esp_wifi_stop plus an NVS
    // commit), and we are inside an event callback, so the frame above would
    // otherwise not reach the panel until after the stall — the screen would
    // freeze on the old content instead of saying what it's doing.
    lv_refr_now(lv_display_get_default());

    serviceProvider_.getNetworkManager().ConnectToStation(ssid, password);
}

void WifiScreen::RefreshTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<WifiScreen*>(lv_timer_get_user_data(t));
    if (!self->IsActive()) return;
    self->CollectScan();
    self->RefreshStatus();
}

void WifiScreen::BackCb(lv_event_t* e)
{
    static_cast<WifiScreen*>(lv_event_get_user_data(e))->navigator_.Go(ScreenId::Settings);
}

void WifiScreen::RescanCb(lv_event_t* e)
{
    static_cast<WifiScreen*>(lv_event_get_user_data(e))->StartScan();
}

void WifiScreen::NetworkCb(lv_event_t* e)
{
    auto* self = static_cast<WifiScreen*>(lv_event_get_user_data(e));
    lv_obj_t* row = lv_event_get_target_obj(e);
    int index = static_cast<int>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row)));
    if (index < 0 || index >= self->scanner_.Count()) return;

    const WiFiInterface::ScanResult& net = self->scanner_.Results()[index];
    if (net.secure)
        self->ShowPasswordView(net.ssid);
    else
        self->Connect(net.ssid, "");   // open network — nothing to ask for
}

void WifiScreen::ConnectCb(lv_event_t* e)
{
    auto* self = static_cast<WifiScreen*>(lv_event_get_user_data(e));
    self->Connect(self->selectedSsid_, lv_textarea_get_text(self->passwordInput_));
}

void WifiScreen::PasswordBackCb(lv_event_t* e)
{
    static_cast<WifiScreen*>(lv_event_get_user_data(e))->ShowListView();
}
