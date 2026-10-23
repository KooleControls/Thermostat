#include "ResetDialog.h"
#include "UiTheme.h"
#include "BleManager/BleManager.h"
#include "SettingsManager/SettingsManager.h"
#include "esp_log.h"
#include "esp_system.h"

static const char* TAG = "ResetDialog";

static constexpr int32_t kCardW   = 480 - 4 * UiTheme::Pad;
static constexpr int32_t kButtonH = 56;

void ResetDialog::Show()
{
    if (IsOpen()) return;

    // The veil takes every touch meant for the screen behind it: a dialog
    // that let the setpoint be dialled underneath it would not be one.
    backdrop_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(backdrop_);
    lv_obj_set_size(backdrop_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop_, LV_OPA_60, 0);
    lv_obj_add_flag(backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(backdrop_);
    lv_obj_set_size(card, kCardW, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, UiTheme::Surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, UiTheme::Line(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, UiTheme::Radius, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_pad_row(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    title_ = lv_label_create(card);
    lv_obj_set_style_text_font(title_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title_, UiTheme::Text(), 0);
    lv_label_set_text(title_, "Reset settings?");

    body_ = lv_label_create(card);
    lv_obj_set_width(body_, LV_PCT(100));
    lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(body_, UiTheme::TextDim(), 0);
    lv_label_set_text(body_, "Every setting goes back to its default, the gateway "
                             "is unpaired, and the thermostat restarts.");

    buttons_ = lv_obj_create(card);
    lv_obj_remove_style_all(buttons_);
    lv_obj_set_size(buttons_, LV_PCT(100), kButtonH);
    lv_obj_set_flex_flow(buttons_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons_, 2 * UiTheme::Gap, 0);
    AddButton(buttons_, "Cancel", UiTheme::SurfacePressed(), UiTheme::Text(), CancelCb, this);
    AddButton(buttons_, "Reset", UiTheme::Danger(), UiTheme::OnAccent(), ResetCb, this);
}

void ResetDialog::Close()
{
    if (backdrop_ == nullptr || resetting_) return;

    // Async: Cancel closes it from inside the button's own click event.
    lv_obj_delete_async(backdrop_);
    backdrop_ = title_ = body_ = buttons_ = nullptr;
}

lv_obj_t* ResetDialog::AddButton(lv_obj_t* parent, const char* text, lv_color_t fill,
                                 lv_color_t ink, lv_event_cb_t cb, void* user)
{
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_height(btn, LV_PCT(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, fill, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    // A filled button that does not move when pressed reads as disabled.
    lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_black(), fill, LV_OPA_20), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, UiTheme::Radius, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);

    lv_obj_t* label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, ink, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

void ResetDialog::CancelCb(lv_event_t* e)
{
    static_cast<ResetDialog*>(lv_event_get_user_data(e))->Close();
}

// Say so first and do it a moment later: the reset blocks the LVGL task through
// two flash writes and a restart, and a dialog that still asks its question
// while that happens looks as if the answer did not land.
void ResetDialog::ResetCb(lv_event_t* e)
{
    auto* self = static_cast<ResetDialog*>(lv_event_get_user_data(e));
    if (self->resetting_) return;
    self->resetting_ = true;

    lv_label_set_text(self->title_, "Restarting...");
    lv_obj_add_flag(self->body_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(self->buttons_, LV_OBJ_FLAG_HIDDEN);

    lv_timer_t* t = lv_timer_create(ResetTimerCb, 300, self);
    lv_timer_set_repeat_count(t, 1);
}

void ResetDialog::ResetTimerCb(lv_timer_t* t)
{
    static_cast<ResetDialog*>(lv_timer_get_user_data(t))->Reset();
}

void ResetDialog::Reset()
{
    ESP_LOGW(TAG, "Resetting settings and restarting (BOOT button)");

    // Unpair before the erase. The settings say which gateway is paired, but
    // the bond itself is NimBLE's; left behind, it would let that gateway pair
    // again without its install code being asked for, so the PIN would never
    // learn it (see PinGate).
    serviceProvider_.getBleManager().Forget();
    serviceProvider_.getSettingsManager().ResetToDefaults();
    esp_restart();
}
