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
// owns the centre. Touching -/+ puts the setpoint there. The first touch only
// shows it and the touches after it dial it; nothing reaches the climate loop
// until the buttons have been left alone for kCommitMs. So a guest can check
// the setpoint without changing it, and a dialled value goes out over
// OpenTherm once, not a degree at a time. Once a press has moved the value —
// even back to where it was — it blinks for the last stretch of that wait,
// after the buttons have been quiet for kSettleMs, to confirm the input is
// about to take effect. A touch that only looked never blinks.
class HomeScreen final : public Screen
{
    static constexpr uint32_t kRefreshMs = 1000;
    static constexpr uint32_t kSettleMs  = 1000;   // quiet time before a dialled value blinks
    static constexpr uint32_t kBlinks    = 3;      // then this many whole blinks, and it goes
    static constexpr uint32_t kCommitMs  = kSettleMs + kBlinks * 2 * HomeFace::kBlinkMs;
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
    static void EditTimerCb(lv_timer_t* t);
    static void IntentTrampoline(void* user, HomeIntent intent);

    ServiceProvider& serviceProvider_;
    Navigator& navigator_;
    HomeFace face_;
    lv_timer_t* refreshTimer_ = nullptr;
    lv_timer_t* editTimer_ = nullptr;   // settle, then commit; see EditTimerCb

    // The edit in progress, if any. The climate loop sees none of it until
    // FinishEdit().
    bool  editing_      = false;
    bool  editSettled_  = false;  // buttons quiet for kSettleMs: a dialled value blinks
    bool  editMoved_    = false;  // a press moved the value, even if it came back
    float editStart_    = 0.0f;   // the setpoint when the edit began
    float editSetpoint_ = 0.0f;   // the value on the dial
};
