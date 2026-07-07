#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "lvgl.h"

// Owns LVGL (via esp_lvgl_port) and the minimal screen: a big room-temperature
// number with -/+ buttons that nudge the setpoint (ClimateManager). Consumes
// the board's panel/touch handles. Headless-safe: no-ops if the panel didn't
// come up. See docs/superpowers/specs/2026-07-07-thermostat-ui-design.md.
class DisplayManager
{
    static constexpr const char *TAG = "DisplayManager";
    static constexpr uint32_t kRevertMs = 4000;    // setpoint shown, then revert
    static constexpr uint32_t kRefreshMs = 1000;   // room-temp refresh cadence

public:
    explicit DisplayManager(ServiceProvider &serviceProvider);

    DisplayManager(const DisplayManager &) = delete;
    DisplayManager &operator=(const DisplayManager &) = delete;
    DisplayManager(DisplayManager &&) = delete;
    DisplayManager &operator=(DisplayManager &&) = delete;

    void Init();

private:
    bool InitLvgl();
    void BuildUi();
    void ShowRoomTemp();          // refresh timer + revert land here
    void OnNudge(float deltaC);   // button handler → ClimateManager + show setpoint

    static void RefreshTimerCb(lv_timer_t *t);
    static void RevertTimerCb(lv_timer_t *t);
    static void MinusCb(lv_event_t *e);
    static void PlusCb(lv_event_t *e);

    ServiceProvider &serviceProvider_;
    InitState initState_;
    lv_display_t *lvDisplay_ = nullptr;
    lv_obj_t *bigLabel_ = nullptr;     // the temperature/setpoint number
    lv_obj_t *stateLabel_ = nullptr;   // "" (temp) or "SET" (setpoint)
    lv_timer_t *revertTimer_ = nullptr;
    bool showingSetpoint_ = false;
};
