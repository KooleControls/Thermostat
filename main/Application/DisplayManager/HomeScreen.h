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
// The big number is the room temperature, with the setpoint on the line
// beneath: a thermostat is read far more often than it is set, so the reading
// owns the centre. Touching -/+ puts the setpoint there, blinking. The first
// touch only shows it and the touches after it dial it; nothing reaches the
// climate loop until the buttons have been left alone for kCommitMs. So a guest
// can check the setpoint without changing it, and a dialled value goes out
// over OpenTherm once, not a degree at a time.
class HomeScreen final : public Screen
{
    static constexpr uint32_t kRefreshMs = 1000;
    static constexpr uint32_t kCommitMs  = 3000;   // quiet time that ends an edit
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
    void FinishEdit();   // hands a dialled setpoint over, if it moved

    static void RefreshTimerCb(lv_timer_t* t);
    static void CommitTimerCb(lv_timer_t* t);
    static void IntentTrampoline(void* user, HomeIntent intent);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    HomeFace face_;
    lv_timer_t* refreshTimer_ = nullptr;
    lv_timer_t* commitTimer_ = nullptr;

    // The edit in progress, if any. The climate loop sees none of it until
    // FinishEdit().
    bool  editing_      = false;
    float editStart_    = 0.0f;   // the setpoint when the edit began
    float editSetpoint_ = 0.0f;   // the value on the dial
};
