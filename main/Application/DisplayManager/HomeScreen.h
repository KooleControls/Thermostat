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
// The big number is the **setpoint** — the value you are setting is the value
// you steer with -/+ — and the measured temperature sits under it. There is no
// transient setpoint view any more.
class HomeScreen final : public Screen
{
    static constexpr uint32_t kRefreshMs = 1000;
    static constexpr float    kNudgeStep = 0.5f;

public:
    HomeScreen(ServiceProvider& serviceProvider, Navigator& navigator)
        : serviceProvider_(serviceProvider), navigator_(navigator) {}

protected:
    void Build(lv_obj_t* root) override;
    void OnShow() override;

private:
    void Refresh();
    void OnIntent(HomeIntent intent);

    static void RefreshTimerCb(lv_timer_t* t);
    static void IntentTrampoline(void* user, HomeIntent intent);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    HomeFace face_;
};
