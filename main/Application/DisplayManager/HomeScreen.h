#pragma once

#include "Screen.h"
#include "ServiceProvider.h"

// The thermostat face: big room-temperature number, − / + setpoint buttons, and
// a gear in the top-right corner that leads into the service menu.
//
// Touching − / + shows the setpoint for a few seconds, then reverts to the room
// temperature. The control surface is ClimateManager::NudgeSetpoint() — this
// screen holds no control state of its own.
class HomeScreen final : public Screen
{
    static constexpr uint32_t kRevertMs = 4000;    // setpoint shown, then revert
    static constexpr uint32_t kRefreshMs = 1000;   // room-temp refresh cadence

public:
    HomeScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;

private:
    void ShowRoomTemp();
    void OnNudge(float deltaC);

    static void RefreshTimerCb(lv_timer_t* t);
    static void RevertTimerCb(lv_timer_t* t);
    static void MinusCb(lv_event_t* e);
    static void PlusCb(lv_event_t* e);
    static void GearCb(lv_event_t* e);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    lv_obj_t* bigLabel_ = nullptr;     // the temperature/setpoint number
    lv_obj_t* stateLabel_ = nullptr;   // "" (temp) or "SET" (setpoint)
    lv_timer_t* revertTimer_ = nullptr;
    bool showingSetpoint_ = false;
};
