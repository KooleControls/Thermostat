#include "SettingsMenuScreen.h"
#include "NetworkManager/NetworkManager.h"
#include "SettingsManager/SettingsManager.h"

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
    lv_obj_set_style_pad_row(list, UiTheme::Gap, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    // The WiFi subtitle is a placeholder only until OnShow runs, which is
    // before the screen is ever on the panel.
    wifiSummary_ = AddRow(list, LV_SYMBOL_WIFI, "WiFi", "not connected", ScreenId::Wifi);
    AddRow(list, LV_SYMBOL_BLUETOOTH, "Gateway", "Pair over Bluetooth", ScreenId::Ble);
    AddRow(list, LV_SYMBOL_LIST, "Info", "Version and hardware", ScreenId::Info);
    AddToggleRow(list, LV_SYMBOL_EYE_OPEN, "Light theme", "Bright palette for daylight",
                 lightTheme_.Get());
}

void SettingsMenuScreen::OnShow()
{
    NetworkManager& net = serviceProvider_.getNetworkManager();
    char ssid[33] = {};
    net.GetStaSsid(ssid, sizeof(ssid));

    // No chevron to re-append: it is a widget of its own on the right of the
    // card now, so the summary is just the row's second line.
    const char* summary = "not connected";
    if (net.IsStaConnected())       summary = ssid;
    else if (net.IsStaConnecting()) summary = "connecting...";
    else if (net.IsAccessPoint())   summary = "own AP";

    SetLabelText(wifiSummary_, summary);
}

lv_obj_t* SettingsMenuScreen::AddRow(lv_obj_t* list, const char* icon, const char* text,
                                     const char* subtitle, ScreenId target)
{
    MenuRow row = AddMenuRow(list, icon, text, subtitle);
    // The target rides in the row's user data — no per-row state to keep.
    lv_obj_add_event_cb(row.card, RowCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(row.card, reinterpret_cast<void*>(static_cast<uintptr_t>(target)));
    return row.subtitle;
}

void SettingsMenuScreen::AddToggleRow(lv_obj_t* list, const char* icon, const char* text,
                                      const char* subtitle, bool on)
{
    // 96 px of reserve rather than the default 44: the switch is 72 wide and
    // the second line has to stop before it.
    MenuRow row = AddMenuRow(list, icon, text, subtitle, false, 96);

    lv_obj_t* sw = lv_switch_create(row.card);
    lv_obj_set_size(sw, 72, 38);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);

    // Off track, on track, knob. Grey reads as "off" against either palette,
    // and the knob is white in both — on light it sits on grey or on blue,
    // never on white.
    lv_obj_set_style_bg_color(sw, UiTheme::TextDim(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, UiTheme::Accent(), UiTheme::Sel(LV_PART_INDICATOR, LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(sw, UiTheme::OnAccent(), LV_PART_KNOB);

    // Display-only: the row owns the gesture, so the switch must not also
    // answer the same tap, and it will never pick up CHECKED on its own.
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);

    lv_obj_add_event_cb(row.card, ThemeCb, LV_EVENT_CLICKED, this);
}

// Persist, then hand the shell the rebuild. Saving first is what makes the
// toggle survive the power cycle that a customer is most likely to try right
// after flipping it.
void SettingsMenuScreen::ThemeCb(lv_event_t* e)
{
    auto* self = static_cast<SettingsMenuScreen*>(lv_event_get_user_data(e));

    lightTheme_.Set(!lightTheme_.Get());
    self->serviceProvider_.getSettingsManager().Save();

    // Deletes this screen's tree — and with it the row whose event is still on
    // the stack — then loads the rebuilt one. Nothing below may touch `self`'s
    // widgets, which is why this is the last statement.
    self->navigator_.Restyle();
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
