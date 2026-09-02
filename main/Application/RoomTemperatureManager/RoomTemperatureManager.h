#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include <cstdint>

// Owns "the measured room temperature": samples the board's ambient
// TemperatureSensor on a fixed cadence and serves cached, validity-checked
// snapshots. Consumers (OpenThermManager now; ClimateManager/UI later) never
// touch the sensor directly — an external source swaps in behind
// GetRoomTemperature() without touching them. Calibration is deliberately
// absent (docs/backlog/room-temp-calibration.md).
//
// That external source now exists, for closing the control loop around a
// simulated room instead of a real one: `room external -temp X` makes
// GetRoomTemperature() answer X. The board sensor keeps being sampled
// underneath either way, so the real reading stays visible (and self-heating
// stays observable) while the simulation drives control.
// Where GetRoomTemperature() takes its answer from.
enum class RoomTempSource : uint8_t
{
    Sensor   = 0,   // the board's ambient sensor (normal operation)
    External = 1,   // a value pushed in over the command surface
};

class RoomTemperatureManager
{
    static constexpr const char *TAG = "RoomTemperatureManager";
    static constexpr int     SampleIntervalMs = 5000;
    static constexpr int64_t ValidityUs = 30LL * 1000 * 1000;  // 6 missed samples

public:
    explicit RoomTemperatureManager(ServiceProvider &serviceProvider);

    RoomTemperatureManager(const RoomTemperatureManager &) = delete;
    RoomTemperatureManager &operator=(const RoomTemperatureManager &) = delete;
    RoomTemperatureManager(RoomTemperatureManager &&) = delete;
    RoomTemperatureManager &operator=(RoomTemperatureManager &&) = delete;

    void Init();

    // false = no valid recent measurement (never read, or stale > 30 s).
    bool GetRoomTemperature(float &celsius) const;

    /// Relative humidity from the same sensor, same cadence and validity rule.
    /// Cached here rather than read at the call site because the AHT20 has one
    /// sampler by design (see Loop()); it is also a second, independent witness
    /// of local self-heating — a warmed sensor reads RH low.
    bool GetRoomHumidity(float &percent) const;

private:
    void Loop();
    RequestError Cmd_RoomTemp(CommandContext& ctx);       // room temp
    RequestError Cmd_RoomExternal(CommandContext& ctx);   // room external
    void WriteExternalStatus(Stream &out);

    // Shared validity rule: a reading is valid if it exists and is no older
    // than ValidityUs. Boundary: exactly ValidityUs old is still valid.
    static bool IsValid(int64_t readUs, int64_t now);

    inline static CommandEntry commands_[] = {
        { "room", "temp",     &InvokeCommand<&RoomTemperatureManager::Cmd_RoomTemp> },
        { "room", "external", &InvokeCommand<&RoomTemperatureManager::Cmd_RoomExternal> },
    };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    float   lastTemp_ = 0.0f;     // last successful reading
    int64_t lastReadUs_ = -1;     // esp_timer time of it; -1 = never read
    bool    lastValid_ = false;   // loop-task-only edge detector for the fault log

    float   lastHumidity_ = 0.0f;
    int64_t lastHumidityUs_ = -1;

    // External source (guarded by mutex_). Deliberately not persisted: a reboot
    // always comes back on the real sensor, so a forgotten simulation cannot
    // survive a power cycle.
    RoomTempSource source_ = RoomTempSource::Sensor;
    float          externalTemp_ = 0.0f;
    int64_t        externalUs_ = -1;
};
