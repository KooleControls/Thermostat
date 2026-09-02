#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include "ClimateMode.h"
#include "PidController.h"
#include <cstdint>

// What the last control step computed — filled by ControlStep(), read by the
// climateStatus command. Grouped so both sides copy one value under the lock.
// (mode_/userSetpoint_ are the live config, kept separate from this mirror.)
struct ClimateStatus
{
    float roomTemp = 0.0f;
    bool  roomValid = false;
    float activeSetpoint = 0.0f;
    float pidOutput = 0.0f;
    float tSet = 0.0f;
    bool  chEnable = false;
    bool  coolEnable = false;
    bool  overrideActive = false;
};

// Owns the thermostat's control logic: mode (Off/Heat/Cool) + setpoint, a
// deadband heating PID producing OpenTherm t_set, on/off cooling demand, and
// remote-override (ID 9) adoption. Consumes RoomTemperatureManager (measured
// temp) and OpenThermManager (boiler caps + override in; heating demand out).
class ClimateManager
{
    static constexpr const char *TAG = "ClimateManager";
    static constexpr int     LoopDelayMs = 5000;
    static constexpr int64_t kCommitIdleUs = 5'000'000;   // defer NVS write until setpoint/mode is quiet this long
    static constexpr float kFrostSetpointC = 5.0f;
    static constexpr float kSetpointMin = 5.0f;
    static constexpr float kSetpointMax = 30.0f;

public:
    explicit ClimateManager(ServiceProvider &serviceProvider);

    ClimateManager(const ClimateManager &) = delete;
    ClimateManager &operator=(const ClimateManager &) = delete;
    ClimateManager(ClimateManager &&) = delete;
    ClimateManager &operator=(ClimateManager &&) = delete;

    void Init();

    // On-screen UI control surface (calls, not JSON commands).
    float GetUserSetpoint() const;
    void  NudgeSetpoint(float deltaC);   // ±, clamped to [5,30]; persisted lazily (see MaybeCommitSettings)
    void  SetUserSetpoint(float celsius);// absolute, same clamping and lazy persistence
    ClimateMode GetMode() const;
    void  SetMode(ClimateMode mode);     // persisted lazily, same as the setpoint

    /// What the last control step computed. Same snapshot the climateStatus
    /// command reports; a copy, so the caller holds no lock.
    ClimateStatus GetStatus() const;

private:
    void Loop();
    void ControlStep();
    void MaybeCommitSettings();   // flush mode/setpoint to NVS once input has gone quiet
    float OutputToTSet(float output, float loBound, float hiBound);
    void  PushSafeState();
    RequestError Cmd_ClimateSet(CommandContext& ctx);
    RequestError Cmd_ClimateStatus(CommandContext& ctx);
    void  WriteStatus(Stream &out);

    inline static CommandEntry commands_[] = {
        { "climate", "set",    &InvokeCommand<&ClimateManager::Cmd_ClimateSet> },
        { "climate", "status", &InvokeCommand<&ClimateManager::Cmd_ClimateStatus> },
    };

    inline static UInt32Setting modeSetting_{ "climate.mode", "Climate Mode", (uint32_t)ClimateMode::Auto };
    inline static FloatSetting  setpointSetting_{ "climate.setpt", "Setpoint", 20.0f };   // NVS key ≤15 chars

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;
    PidController pid_;

    // live config + last-step snapshot (guarded by mutex_)
    ClimateMode mode_ = ClimateMode::Auto;
    float userSetpoint_ = 20.0f;
    ClimateStatus status_;

    // deferred NVS persistence (guarded by mutex_): changes update RAM instantly
    // and mark dirty; the loop flushes one write once input is quiet.
    bool    settingsDirty_ = false;
    int64_t lastChangeUs_ = 0;

    // loop-task-only
    int64_t lastStepUs_ = -1;
    bool    lastSafe_ = false;   // fault-log edge detector
};
