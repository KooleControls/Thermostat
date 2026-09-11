#pragma once

#include "HomeFace.h"
#include "Screen.h"
#include "ServiceProvider.h"

// The thermostat face's data half.
//
// The drawing lives in HomeFace, which knows nothing about managers; this class
// is the seam to the rest of the firmware: once a second it reads the setpoint,
// the room temperature, the boiler's status bits and the gateway link into a
// HomeView, and it turns the intents a finger raises back into manager calls.
//
// The big number is the room temperature. Touching -/+ swaps it for the
// setpoint for a few seconds, then it reverts on its own: a thermostat is read
// far more often than it is set, so the reading owns the centre.
class HomeScreen final : public Screen
{
    static constexpr uint32_t kRefreshMs = 1000;
    static constexpr uint32_t kRevertMs  = 4000;   // setpoint shown, then back
    static constexpr float    kNudgeStep = 1.0f;   // whole degrees, as displayed

public:
    HomeScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;
    void OnDestroy() override;

private:
    void Refresh();
    void OnIntent(HomeIntent intent);
    void Nudge(float direction);

    static void RefreshTimerCb(lv_timer_t* t);
    static void RevertTimerCb(lv_timer_t* t);
    static void IntentTrampoline(void* user, HomeIntent intent);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    HomeFace face_;
    lv_timer_t* refreshTimer_ = nullptr;
    lv_timer_t* revertTimer_ = nullptr;
    bool showingSetpoint_ = false;
};
