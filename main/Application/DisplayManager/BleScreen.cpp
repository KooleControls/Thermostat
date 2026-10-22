#include "BleScreen.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "BleScreen";

// A gateway in one phrase, the way the list names it: by name, then by gateway
// id, and by address only when it gave neither.
static void DescribePeer(const BleManager::Peer& peer, char* out, size_t cap)
{
    const bool hasId = peer.hasDgid && peer.dgid != 0;
    const unsigned long id = static_cast<unsigned long>(peer.dgid);

    if (peer.name[0] != '\0' && hasId) snprintf(out, cap, "%s, id %lu", peer.name, id);
    else if (peer.name[0] != '\0')     snprintf(out, cap, "%s", peer.name);
    else if (hasId)                    snprintf(out, cap, "gateway id %lu", id);
    else                               BleManager::FormatAddr(peer.addr, out, cap);
}

void BleScreen::Build(lv_obj_t* root)
{
    lv_obj_t* back = AddHeader(root, "Gateway", LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, BackCb, LV_EVENT_CLICKED, this);
    scanBar_.Build(root);

    statusLabel_ = lv_label_create(root);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(statusLabel_, UiTheme::TextDim(), 0);
    lv_obj_set_width(statusLabel_, 480 - 2 * UiTheme::Pad);
    lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(statusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(statusLabel_, LV_ALIGN_TOP_MID, 0, UiTheme::HeaderH);
    lv_label_set_text(statusLabel_, "");
    statusWarn_ = false;   // a new label, in TextDim()

    // ── List view: rescan + forget, then the gateways we heard ────
    listView_ = lv_obj_create(root);
    lv_obj_set_size(listView_, LV_PCT(100), 480 - UiTheme::HeaderH - 32);
    lv_obj_align(listView_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(listView_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listView_, 0, 0);
    lv_obj_set_style_pad_all(listView_, UiTheme::Pad, 0);
    lv_obj_remove_flag(listView_, LV_OBJ_FLAG_SCROLLABLE);

    rescanButton_ = AddCard(listView_, 56);
    lv_obj_set_width(rescanButton_, 200);
    lv_obj_align(rescanButton_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(rescanButton_, RescanCb, LV_EVENT_CLICKED, this);
    lv_obj_t* rescanLabel = lv_label_create(rescanButton_);
    lv_obj_set_style_text_font(rescanLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(rescanLabel, UiTheme::Text(), 0);
    lv_label_set_text(rescanLabel, LV_SYMBOL_REFRESH "  Scan again");
    lv_obj_center(rescanLabel);

    // Unpair: drops the link and deletes the bond, so the next pairing asks for
    // the install code again. Dead while nothing is paired.
    forgetButton_ = AddCard(listView_, 56);
    lv_obj_set_width(forgetButton_, 200);
    lv_obj_align(forgetButton_, LV_ALIGN_TOP_RIGHT, 0, 0);
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
    lv_obj_set_style_bg_color(connect, UiTheme::AccentPressed(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(connect, UiTheme::Radius, 0);
    lv_obj_set_style_shadow_width(connect, 0, 0);
    lv_obj_add_event_cb(connect, ConnectCb, LV_EVENT_CLICKED, this);
    lv_obj_t* connectLabel = lv_label_create(connect);
    lv_obj_set_style_text_font(connectLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(connectLabel, UiTheme::OnAccent(), 0);
    lv_label_set_text(connectLabel, "Pair");
    lv_obj_center(connectLabel);

    lv_obj_t* keypad = lv_keyboard_create(codeView_);
    lv_keyboard_set_mode(keypad, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(keypad, LV_PCT(100), 240);
    lv_obj_align(keypad, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keypad, codeInput_);
    lv_obj_add_event_cb(keypad, ConnectCb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keypad, CodeBackCb, LV_EVENT_CANCEL, this);

    // Once for the life of the screen — see HomeScreen::Build.
    if (refreshTimer_ == nullptr)
        refreshTimer_ = lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void BleScreen::OnShow()
{
    ShowListView();
    BleManager& ble = serviceProvider_.getBleManager();
    ble.StartScan();
    // No scan (the stack is down): draw what the manager last heard, which may
    // have changed while this screen was away.
    if (!ble.Scanning()) listStale_ = true;
    lastLink_ = ble.GetLinkState();   // a link already up is not news
    RefreshStatus();
}

// The status line belongs to the list view. It sits where the code view's
// prompt does, so left up it drew straight through "Install code for ...".
void BleScreen::ShowListView()
{
    lv_obj_add_flag(codeView_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(listView_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(statusLabel_, LV_OBJ_FLAG_HIDDEN);
}

void BleScreen::ShowCodeView(const BleManager::Peer& peer)
{
    selected_ = peer;      // copy: a later scan must not move the target

    const char* label = selected_.name[0] != '\0' ? selected_.name : "gateway";
    lv_label_set_text_fmt(codePrompt_, "Install code for %s", label);
    lv_textarea_set_text(codeInput_, "");

    lv_obj_add_flag(listView_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(statusLabel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(codeView_, LV_OBJ_FLAG_HIDDEN);
}

void BleScreen::RefreshStatus()
{
    BleManager& ble = serviceProvider_.getBleManager();

    // A scan already running is not restarted.
    bool scanning = ble.Scanning();
    if (scanning) lv_obj_add_state(rescanButton_, LV_STATE_DISABLED);
    else          lv_obj_remove_state(rescanButton_, LV_STATE_DISABLED);

    // The scan line follows the scan, whoever started it. A BLE scan runs for a
    // fixed time from a known start, so the line is exact, including for a scan
    // the web UI began before this screen was opened; one cut short by a
    // pairing finishes early.
    if (scanning && !scanBar_.Running())
        scanBar_.Start(static_cast<uint32_t>(BleManager::ScanDurationMs), ble.ScanElapsedMs());
    else if (!scanning && scanBar_.Running())
        scanBar_.Finish();

    // Unpair is live whenever something is paired, which includes the gaps
    // between reconnect attempts to a gateway that is off — the moment someone
    // is most likely to want it — and is meaningless with nothing paired.
    BleManager::Peer paired;
    const bool isPaired = ble.GetPaired(paired);
    if (isPaired) lv_obj_remove_state(forgetButton_, LV_STATE_DISABLED);
    else          lv_obj_add_state(forgetButton_, LV_STATE_DISABLED);

    char who[56];
    if (isPaired) DescribePeer(paired, who, sizeof(who));
    else          snprintf(who, sizeof(who), "the gateway");

    // A link coming up has just written to flash — a new bond, at the end of a
    // pairing — and that is when the picture shifted. This screen does not
    // change, so the shell's resync after a screen change would not come.
    const BleManager::LinkState link = ble.GetLinkState();
    if (link == BleManager::LinkState::Ready && lastLink_ != BleManager::LinkState::Ready)
        navigator_.ResyncPanel();
    lastLink_ = link;

    char status[96];
    bool warn = false;
    switch (link)
    {
        case BleManager::LinkState::Ready:
            snprintf(status, sizeof(status), LV_SYMBOL_OK "  Connected to %s", who);
            break;
        case BleManager::LinkState::Connecting:
            snprintf(status, sizeof(status), "Connecting to %s...", who);
            break;
        case BleManager::LinkState::Securing:
            snprintf(status, sizeof(status), "Pairing with %s...", who);
            break;
        case BleManager::LinkState::Discovering:
            snprintf(status, sizeof(status), "Reading the gateway's services...");
            break;
        case BleManager::LinkState::Down:
        {
            BleManager::Peer failed;
            if (scanning)      snprintf(status, sizeof(status), "Looking for gateways...");
            else if (isPaired) snprintf(status, sizeof(status), "Paired with %s, not connected", who);
            else if (ble.GetPairFailure(&failed) == BleManager::PairFailure::WrongCode)
            {
                char failedWho[56];
                DescribePeer(failed, failedWho, sizeof(failedWho));
                snprintf(status, sizeof(status), "Wrong install code for %s", failedWho);
                warn = true;
            }
            else               snprintf(status, sizeof(status), "Not connected: pick a gateway");
            break;
        }
    }
    SetLabelText(statusLabel_, status);

    // Red only for what the installer has to act on; compared first, because
    // setting a style redraws the label even when nothing changed.
    if (warn != statusWarn_)
    {
        lv_obj_set_style_text_color(statusLabel_, warn ? UiTheme::Danger() : UiTheme::TextDim(), 0);
        statusWarn_ = warn;
    }

    // The list follows the scan, as on the WiFi screen: emptied when one starts,
    // and drawn once, not every half second, when it ends. Emptied whoever
    // started it — the web UI's `ble scan` too — because the manager empties
    // its own table then, and rows left up would be gateways it no longer has.
    if (scanning && !listStale_)
    {
        lv_obj_clean(peerList_);
        listStale_ = true;
    }
    else if (!scanning && listStale_)
    {
        listStale_ = false;
        RebuildPeerList();
    }
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

        lv_obj_t* row = AddCard(peerList_, 60);
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

    serviceProvider_.getBleManager().Connect(selected_, code);   // name and id too

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
    self->serviceProvider_.getBleManager().StartScan();
    self->RefreshStatus();   // empties the list now, not on the next tick
}

void BleScreen::ForgetCb(lv_event_t* e)
{
    auto* self = static_cast<BleScreen*>(lv_event_get_user_data(e));
    self->serviceProvider_.getBleManager().Forget();   // deletes the bond: a flash write
    self->navigator_.ResyncPanel();
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
