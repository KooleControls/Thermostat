#include "HomeScreen.h"
#include "ClimateManager/ClimateManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include <cstdio>

extern "C" const lv_font_t font_temp_96;

void HomeScreen::Build(lv_obj_t* root)
{
    lv_obj_t* gear = AddIconButton(root, LV_SYMBOL_SETTINGS);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -UiTheme::Pad / 2, UiTheme::Pad / 2);
    lv_obj_add_event_cb(gear, GearCb, LV_EVENT_CLICKED, this);

    stateLabel_ = lv_label_create(root);
    lv_obj_set_style_text_color(stateLabel_, UiTheme::TextDim(), 0);
    lv_obj_set_style_text_font(stateLabel_, &lv_font_montserrat_28, 0);
    lv_label_set_text(stateLabel_, "");
    lv_obj_align(stateLabel_, LV_ALIGN_TOP_MID, 0, 60);

    bigLabel_ = lv_label_create(root);
    lv_obj_set_style_text_color(bigLabel_, UiTheme::Text(), 0);
    lv_obj_set_style_text_font(bigLabel_, &font_temp_96, 0);
    lv_obj_align(bigLabel_, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t* minus = lv_button_create(root);
    lv_obj_set_size(minus, 200, 130);
    lv_obj_align(minus, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_add_event_cb(minus, MinusCb, LV_EVENT_CLICKED, this);
    lv_obj_t* ml = lv_label_create(minus);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_48, 0);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    lv_obj_t* plus = lv_button_create(root);
    lv_obj_set_size(plus, 200, 130);
    lv_obj_align(plus, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_add_event_cb(plus, PlusCb, LV_EVENT_CLICKED, this);
    lv_obj_t* pl = lv_label_create(plus);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_48, 0);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void HomeScreen::OnShow()
{
    // Coming back from the menu always lands on the room temperature, never on
    // a stale "SET" view.
    if (revertTimer_)
    {
        lv_timer_delete(revertTimer_);
        revertTimer_ = nullptr;
    }
    showingSetpoint_ = false;
    ShowRoomTemp();
}

void HomeScreen::ShowRoomTemp()
{
    float t = 0;
    bool valid = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(t);
    char buf[16];
    if (valid) snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", t);   // UTF-8 degree
    else       snprintf(buf, sizeof(buf), "--.-\xC2\xB0");

    lv_label_set_text(stateLabel_, "");
    lv_label_set_text(bigLabel_, buf);
    showingSetpoint_ = false;
}

void HomeScreen::OnNudge(float deltaC)
{
    serviceProvider_.getClimateManager().NudgeSetpoint(deltaC);
    float sp = serviceProvider_.getClimateManager().GetUserSetpoint();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", sp);

    lv_label_set_text(stateLabel_, "SET");
    lv_label_set_text(bigLabel_, buf);
    if (revertTimer_) lv_timer_reset(revertTimer_);
    else revertTimer_ = lv_timer_create(RevertTimerCb, kRevertMs, this);
    showingSetpoint_ = true;
}

void HomeScreen::RefreshTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<HomeScreen*>(lv_timer_get_user_data(t));
    if (!self->IsActive()) return;           // menu is on screen — nothing to refresh
    if (self->showingSetpoint_) return;      // don't clobber the setpoint view
    self->ShowRoomTemp();
}

void HomeScreen::RevertTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<HomeScreen*>(lv_timer_get_user_data(t));
    lv_timer_delete(t);
    self->revertTimer_ = nullptr;
    self->ShowRoomTemp();
}

void HomeScreen::MinusCb(lv_event_t* e)
{
    static_cast<HomeScreen*>(lv_event_get_user_data(e))->OnNudge(-0.5f);
}

void HomeScreen::PlusCb(lv_event_t* e)
{
    static_cast<HomeScreen*>(lv_event_get_user_data(e))->OnNudge(+0.5f);
}

void HomeScreen::GearCb(lv_event_t* e)
{
    auto* self = static_cast<HomeScreen*>(lv_event_get_user_data(e));
    self->navigator_.Go(ScreenId::Pin);   // shell skips the pad when no PIN is set
}
