#include "SettingsMenuScreen.h"

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

    // Both pending rows are the shared shell the WiFi backlog items plug into
    // (docs/backlog/2026-07-27-wifi-connect-ui.md and -wifi-update-ui.md).
    AddPendingRow(list, LV_SYMBOL_WIFI, "WiFi");
    AddPendingRow(list, LV_SYMBOL_DOWNLOAD, "Firmware");
    AddRow(list, LV_SYMBOL_LIST, "Info", ScreenId::Info);
}

lv_obj_t* SettingsMenuScreen::MakeRow(lv_obj_t* list, const char* icon, const char* text,
                                     const char* trailing)
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
    lv_label_set_text(tail, trailing);
    lv_obj_align(tail, LV_ALIGN_RIGHT_MID, 0, 0);

    return row;
}

lv_obj_t* SettingsMenuScreen::AddRow(lv_obj_t* list, const char* icon, const char* text,
                                     ScreenId target)
{
    lv_obj_t* row = MakeRow(list, icon, text, LV_SYMBOL_RIGHT);
    // The target rides in the event user data — no per-row state to keep.
    lv_obj_add_event_cb(row, RowCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(target)));
    return row;
}

lv_obj_t* SettingsMenuScreen::AddPendingRow(lv_obj_t* list, const char* icon, const char* text)
{
    lv_obj_t* row = MakeRow(list, icon, text, "soon");
    lv_obj_add_state(row, LV_STATE_DISABLED);
    return row;
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
