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

// Owns the thermostat's control logic: mode (Off/Heat/Cool) + setpoint, a
// deadband heating PID producing OpenTherm t_set, on/off cooling demand, and
// remote-override (ID 9) adoption. Consumes RoomTemperatureManager (measured
// temp) and OpenThermManager (boiler caps + override in; heating demand out).
class ClimateManager
{
    static constexpr const char *TAG = "ClimateManager";
    static constexpr int   LoopDelayMs = 5000;
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

private:
    void Loop();
    void ControlStep();
    float OutputToTSet(float output, float loBound, float hiBound);
    void  PushSafeState();
    void  Cmd_ClimateSet(Stream &in, Stream &out);
    void  Cmd_ClimateStatus(Stream &in, Stream &out);
    void  WriteStatus(Stream &out);

    inline static CommandEntry commands_[] = {
        { "climateSet",    &InvokeCommand<&ClimateManager::Cmd_ClimateSet> },
        { "climateStatus", &InvokeCommand<&ClimateManager::Cmd_ClimateStatus> },
    };

    inline static UInt32Setting modeSetting_{ "climate.mode", "Climate Mode", (uint32_t)ClimateMode::Off };
    inline static FloatSetting  setpointSetting_{ "climate.setpoint", "Setpoint", 20.0f };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;
    PidController pid_;

    // state (guarded by mutex_)
    ClimateMode mode_ = ClimateMode::Off;
    float userSetpoint_ = 20.0f;
    float lastRoomTemp_ = 0.0f;
    bool  lastRoomValid_ = false;
    float lastActiveSetpoint_ = 0.0f;
    float lastPidOutput_ = 0.0f;
    float lastTSet_ = 0.0f;
    bool  lastChEnable_ = false;
    bool  lastCoolEnable_ = false;
    bool  lastOverrideActive_ = false;

    // loop-task-only
    int64_t lastStepUs_ = -1;
    bool    lastSafe_ = false;   // fault-log edge detector
};
