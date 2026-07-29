#include "SettingsMenuScreen.h"
#include "NetworkManager/NetworkManager.h"

void SettingsMenuScreen::Build(lv_obj_t* root)
{
    lv_obj_t* close = AddHeader(root, "Settings", LV_SYMBOL_CLOSE);
    lv_obj_add_event_cb(close, CloseCb, LV_EVENT_CLICKED, this);

    lv_obj_t* list = lv_obj_create(root);
    lv_obj_set_size(list, LV_PCT(100), 480 - UiTheme::HeaderH);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, UiTheme::Pad, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    wifiSummary_ = AddRow(list, LV_SYMBOL_WIFI, "WiFi", ScreenId::Wifi);
    AddRow(list, LV_SYMBOL_BLUETOOTH, "Gateway", ScreenId::Ble);
    // Still the shared shell the firmware-update item plugs into
    // (docs/backlog/2026-07-27-wifi-update-ui.md).
    AddPendingRow(list, LV_SYMBOL_DOWNLOAD, "Firmware");
    AddRow(list, LV_SYMBOL_LIST, "Info", ScreenId::Info);
}

void SettingsMenuScreen::OnShow()
{
    NetworkManager& net = serviceProvider_.getNetworkManager();
    char ssid[33] = {};
    net.GetStaSsid(ssid, sizeof(ssid));

    // Keep the chevron: the summary replaces the row's trailing label, and the
    // row still has to read as "leads somewhere".
    const char* summary = "not connected";
    if (net.IsStaConnected())       summary = ssid;
    else if (net.IsStaConnecting()) summary = "connecting...";
    else if (net.IsAccessPoint())   summary = "own AP";

    lv_label_set_text_fmt(wifiSummary_, "%s  " LV_SYMBOL_RIGHT, summary);
}

lv_obj_t* SettingsMenuScreen::MakeRow(lv_obj_t* list, const char* icon, const char* text,
                                      const char* trailing, lv_obj_t** trailingLabel)
{
    lv_obj_t* row = lv_button_create(list);
    lv_obj_set_size(row, LV_PCT(100), UiTheme::RowH);
    lv_obj_set_style_bg_color(row, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_color(row, UiTheme::Accent(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, UiTheme::Pad, 0);

    lv_obj_t* label = lv_label_create(row);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, UiTheme::Text(), 0);
    lv_label_set_text_fmt(label, "%s  %s", icon, text);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* tail = lv_label_create(row);
    lv_obj_set_style_text_font(tail, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tail, UiTheme::TextDim(), 0);
    lv_obj_set_width(tail, 200);
    lv_label_set_long_mode(tail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(tail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(tail, trailing);
    lv_obj_align(tail, LV_ALIGN_RIGHT_MID, 0, 0);

    if (trailingLabel) *trailingLabel = tail;
    return row;
}

lv_obj_t* SettingsMenuScreen::AddRow(lv_obj_t* list, const char* icon, const char* text,
                                     ScreenId target)
{
    lv_obj_t* tail = nullptr;
    lv_obj_t* row = MakeRow(list, icon, text, LV_SYMBOL_RIGHT, &tail);
    // The target rides in the row's user data — no per-row state to keep.
    lv_obj_add_event_cb(row, RowCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(target)));
    return tail;
}

void SettingsMenuScreen::AddPendingRow(lv_obj_t* list, const char* icon, const char* text)
{
    lv_obj_t* row = MakeRow(list, icon, text, "soon", nullptr);
    lv_obj_add_state(row, LV_STATE_DISABLED);
}

void SettingsMenuScreen::RowCb(lv_event_t* e)
{
    auto* self = static_cast<SettingsMenuScreen*>(lv_event_get_user_data(e));
    lv_obj_t* row = lv_event_get_target_obj(e);
    auto target = static_cast<ScreenId>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row)));
    self->navigator_.Go(target);
}

void SettingsMenuScreen::CloseCb(lv_event_t* e)
{
    static_cast<SettingsMenuScreen*>(lv_event_get_user_data(e))->navigator_.Go(ScreenId::Home);
}
