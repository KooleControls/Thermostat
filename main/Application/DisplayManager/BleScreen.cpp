#include "BleScreen.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "BleScreen";

void BleScreen::Build(lv_obj_t* root)
{
    lv_obj_t* back = AddHeader(root, "Gateway", LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, BackCb, LV_EVENT_CLICKED, this);

    statusLabel_ = lv_label_create(root);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(statusLabel_, UiTheme::TextDim(), 0);
    lv_obj_set_width(statusLabel_, 480 - 2 * UiTheme::Pad);
    lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(statusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(statusLabel_, LV_ALIGN_TOP_MID, 0, UiTheme::HeaderH);
    lv_label_set_text(statusLabel_, "");

    // ── List view: rescan + forget, then the gateways we heard ────
    listView_ = lv_obj_create(root);
    lv_obj_set_size(listView_, LV_PCT(100), 480 - UiTheme::HeaderH - 32);
    lv_obj_align(listView_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(listView_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listView_, 0, 0);
    lv_obj_set_style_pad_all(listView_, UiTheme::Pad, 0);
    lv_obj_remove_flag(listView_, LV_OBJ_FLAG_SCROLLABLE);

    rescanButton_ = lv_button_create(listView_);
    lv_obj_set_size(rescanButton_, 200, 56);
    lv_obj_align(rescanButton_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(rescanButton_, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_color(rescanButton_, UiTheme::Accent(), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(rescanButton_, 0, 0);
    lv_obj_add_event_cb(rescanButton_, RescanCb, LV_EVENT_CLICKED, this);
    lv_obj_t* rescanLabel = lv_label_create(rescanButton_);
    lv_obj_set_style_text_font(rescanLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(rescanLabel, UiTheme::Text(), 0);
    lv_label_set_text(rescanLabel, LV_SYMBOL_REFRESH "  Scan again");
    lv_obj_center(rescanLabel);

    // Unpair: drops the link and deletes the bond, so the next pairing asks for
    // the install code again. Dead while nothing is paired.
    forgetButton_ = lv_button_create(listView_);
    lv_obj_set_size(forgetButton_, 200, 56);
    lv_obj_align(forgetButton_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(forgetButton_, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_color(forgetButton_, UiTheme::Accent(), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(forgetButton_, 0, 0);
    lv_obj_add_event_cb(forgetButton_, ForgetCb, LV_EVENT_CLICKED, this);
    lv_obj_t* forgetLabel = lv_label_create(forgetButton_);
    lv_obj_set_style_text_font(forgetLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(forgetLabel, UiTheme::Text(), 0);
    lv_label_set_text(forgetLabel, LV_SYMBOL_TRASH "  Unpair");
    lv_obj_center(forgetLabel);

    peerList_ = lv_obj_create(listView_);
    lv_obj_set_size(peerList_, LV_PCT(100),
                    480 - UiTheme::HeaderH - 32 - 2 * UiTheme::Pad - 64);
    lv_obj_align(peerList_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(peerList_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(peerList_, 0, 0);
    lv_obj_set_style_pad_all(peerList_, 0, 0);
    lv_obj_set_style_pad_row(peerList_, 6, 0);
    lv_obj_set_flex_flow(peerList_, LV_FLEX_FLOW_COLUMN);

    // ── Code view: which gateway, its install code, a number pad ──
    codeView_ = lv_obj_create(root);
    lv_obj_set_size(codeView_, LV_PCT(100), 480 - UiTheme::HeaderH);
    lv_obj_align(codeView_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(codeView_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(codeView_, 0, 0);
    lv_obj_set_style_pad_all(codeView_, 0, 0);
    lv_obj_remove_flag(codeView_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(codeView_, LV_OBJ_FLAG_HIDDEN);

    codePrompt_ = lv_label_create(codeView_);
    lv_obj_set_style_text_font(codePrompt_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(codePrompt_, UiTheme::Text(), 0);
    lv_obj_set_width(codePrompt_, 480 - 2 * UiTheme::Pad);
    lv_label_set_long_mode(codePrompt_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(codePrompt_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(codePrompt_, LV_ALIGN_TOP_MID, 0, 4);

    codeInput_ = lv_textarea_create(codeView_);
    lv_textarea_set_one_line(codeInput_, true);
    lv_textarea_set_max_length(codeInput_, 6);          // the code is 6 digits
    lv_textarea_set_accepted_chars(codeInput_, "0123456789");
    lv_textarea_set_placeholder_text(codeInput_, "Install code");
    lv_obj_set_size(codeInput_, 480 - 2 * UiTheme::Pad, 52);
    lv_obj_align(codeInput_, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_text_font(codeInput_, &lv_font_montserrat_20, 0);

    lv_obj_t* connect = lv_button_create(codeView_);
    lv_obj_set_size(connect, 200, 52);
    lv_obj_align(connect, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_bg_color(connect, UiTheme::Accent(), 0);
    lv_obj_set_style_shadow_width(connect, 0, 0);
    lv_obj_add_event_cb(connect, ConnectCb, LV_EVENT_CLICKED, this);
    lv_obj_t* connectLabel = lv_label_create(connect);
    lv_obj_set_style_text_font(connectLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(connectLabel, UiTheme::Text(), 0);
    lv_label_set_text(connectLabel, "Pair");
    lv_obj_center(connectLabel);

    lv_obj_t* keypad = lv_keyboard_create(codeView_);
    lv_keyboard_set_mode(keypad, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(keypad, LV_PCT(100), 240);
    lv_obj_align(keypad, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keypad, codeInput_);
    lv_obj_add_event_cb(keypad, ConnectCb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keypad, CodeBackCb, LV_EVENT_CANCEL, this);

    lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void BleScreen::OnShow()
{
    ShowListView();
    RefreshStatus();
    listStale_ = true;
    serviceProvider_.getBleManager().StartScan();
}

void BleScreen::ShowListView()
{
    lv_obj_add_flag(codeView_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(listView_, LV_OBJ_FLAG_HIDDEN);
}

void BleScreen::ShowCodeView(const BleManager::Peer& peer)
{
    selected_ = peer;      // copy: a later scan must not move the target

    const char* label = selected_.name[0] != '\0' ? selected_.name : "gateway";
    lv_label_set_text_fmt(codePrompt_, "Install code for %s", label);
    lv_textarea_set_text(codeInput_, "");

    lv_obj_add_flag(listView_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(codeView_, LV_OBJ_FLAG_HIDDEN);
}

void BleScreen::RefreshStatus()
{
    BleManager& ble = serviceProvider_.getBleManager();

    // Rescanning while connecting would cancel the attempt, and unpairing is
    // meaningless with nothing paired.
    bool scanning = ble.Scanning();
    if (scanning) lv_obj_add_state(rescanButton_, LV_STATE_DISABLED);
    else          lv_obj_remove_state(rescanButton_, LV_STATE_DISABLED);

    switch (ble.GetLinkState())
    {
        case BleManager::LinkState::Ready:
            lv_label_set_text(statusLabel_, LV_SYMBOL_OK "  Paired and connected");
            lv_obj_remove_state(forgetButton_, LV_STATE_DISABLED);
            break;
        case BleManager::LinkState::Connecting:
            lv_label_set_text(statusLabel_, "Connecting...");
            lv_obj_remove_state(forgetButton_, LV_STATE_DISABLED);
            break;
        case BleManager::LinkState::Securing:
            lv_label_set_text(statusLabel_, "Pairing...");
            lv_obj_remove_state(forgetButton_, LV_STATE_DISABLED);
            break;
        case BleManager::LinkState::Discovering:
            lv_label_set_text(statusLabel_, "Reading the gateway's services...");
            lv_obj_remove_state(forgetButton_, LV_STATE_DISABLED);
            break;
        case BleManager::LinkState::Down:
            lv_label_set_text(statusLabel_,
                              scanning ? "Looking for gateways..."
                                       : "Not connected — pick a gateway");
            lv_obj_add_state(forgetButton_, LV_STATE_DISABLED);
            break;
    }

    // A scan that has finished gets drawn once, not every half second.
    if (!scanning && listStale_)
    {
        listStale_ = false;
        RebuildPeerList();
    }
    if (scanning) listStale_ = true;
}

void BleScreen::RebuildPeerList()
{
    lv_obj_clean(peerList_);

    BleManager::Peer peers[BleManager::MaxPeers];
    shownCount_ = serviceProvider_.getBleManager().GetPeers(peers, BleManager::MaxPeers);

    if (shownCount_ == 0)
    {
        lv_obj_t* empty = lv_label_create(peerList_);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(empty, UiTheme::TextDim(), 0);
        lv_label_set_text(empty, "No gateways found");
        return;
    }

    for (int i = 0; i < shownCount_; i++)
    {
        const BleManager::Peer& peer = peers[i];

        lv_obj_t* row = lv_button_create(peerList_);
        lv_obj_set_size(row, LV_PCT(100), 60);
        lv_obj_set_style_bg_color(row, UiTheme::Surface(), 0);
        lv_obj_set_style_bg_color(row, UiTheme::Accent(), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_hor(row, UiTheme::Pad, 0);
        lv_obj_add_event_cb(row, PeerCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        // Name is the primary line; the gateway id sits under it, and the address
        // stands in when a unit has neither (both default to the same value on
        // every gateway) — see the advertisement note.
        lv_obj_t* name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(name, UiTheme::Text(), 0);
        lv_obj_set_width(name, 300);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(name, peer.name[0] != '\0' ? peer.name : "(unnamed gateway)");
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, -10);

        char addr[18];
        BleManager::FormatAddr(peer.addr, addr, sizeof(addr));

        lv_obj_t* sub = lv_label_create(row);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(sub, UiTheme::TextDim(), 0);
        lv_obj_set_width(sub, 300);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
        if (peer.hasDgid && peer.dgid != 0)
            lv_label_set_text_fmt(sub, "id %lu", static_cast<unsigned long>(peer.dgid));
        else
            lv_label_set_text(sub, addr);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 0, 12);

        lv_obj_t* tail = lv_label_create(row);
        lv_obj_set_style_text_font(tail, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tail, UiTheme::TextDim(), 0);
        lv_label_set_text_fmt(tail, "%d dBm", peer.rssi);
        lv_obj_align(tail, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

void BleScreen::Connect()
{
    const char* code = lv_textarea_get_text(codeInput_);

    char addr[18];
    BleManager::FormatAddr(selected_.addr, addr, sizeof(addr));
    ESP_LOGI(TAG, "Pairing with %s", addr);

    serviceProvider_.getBleManager().Connect(selected_.addr, code);

    ShowListView();
    RefreshStatus();
}

// ──────────────────────────────────────────────────────────────
// Callbacks
// ──────────────────────────────────────────────────────────────

void BleScreen::RefreshTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<BleScreen*>(lv_timer_get_user_data(t));
    if (!self || !self->IsActive()) return;
    self->RefreshStatus();
}

void BleScreen::BackCb(lv_event_t* e)
{
    static_cast<BleScreen*>(lv_event_get_user_data(e))->navigator_.Go(ScreenId::Settings);
}

void BleScreen::RescanCb(lv_event_t* e)
{
    auto* self = static_cast<BleScreen*>(lv_event_get_user_data(e));
    self->listStale_ = true;
    self->serviceProvider_.getBleManager().StartScan();
    self->RefreshStatus();
}

void BleScreen::ForgetCb(lv_event_t* e)
{
    auto* self = static_cast<BleScreen*>(lv_event_get_user_data(e));
    self->serviceProvider_.getBleManager().Forget();
    self->RefreshStatus();
}

void BleScreen::PeerCb(lv_event_t* e)
{
    auto* self = static_cast<BleScreen*>(lv_event_get_user_data(e));
    lv_obj_t* row = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int index = static_cast<int>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row)));

    BleManager::Peer peers[BleManager::MaxPeers];
    int count = self->serviceProvider_.getBleManager().GetPeers(peers, BleManager::MaxPeers);
    if (index < 0 || index >= count) return;   // list moved under the tap

    self->ShowCodeView(peers[index]);
}

void BleScreen::ConnectCb(lv_event_t* e)
{
    static_cast<BleScreen*>(lv_event_get_user_data(e))->Connect();
}

void BleScreen::CodeBackCb(lv_event_t* e)
{
    static_cast<BleScreen*>(lv_event_get_user_data(e))->ShowListView();
}
