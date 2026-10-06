#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
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

    // ── Smoothing ─────────────────────────────────────────────────────────
    //
    // What leaves this manager is a first-order lag over the sensor, not the
    // raw reading. The AHT20 dithers by a few hundredths between samples and
    // the room does not: at 5 s a piece that noise is the only thing moving,
    // and everything downstream reacts to it -- the PID sees it as error, the
    // OT link ships it to the gateway, and the gateway writes a log record for
    // every twitch.
    //
    // A first-order lag rather than a sliding window: one float of state
    // instead of a ring, no step when a sample is dropped, and a time constant
    // that means something physical. Tau is how long it takes to cover 63 % of
    // a step -- and it is also the lag this adds, which is why it is seconds
    // rather than minutes. A room's own response is tens of minutes, so a
    // minute of filter is invisible to the control loop; five would start to be
    // real dead time in front of a door being opened.
    static constexpr float DefaultFilterTauS = 60.0f;

public:
    explicit RoomTemperatureManager(ServiceProvider &serviceProvider);

    RoomTemperatureManager(const RoomTemperatureManager &) = delete;
    RoomTemperatureManager &operator=(const RoomTemperatureManager &) = delete;
    RoomTemperatureManager(RoomTemperatureManager &&) = delete;
    RoomTemperatureManager &operator=(RoomTemperatureManager &&) = delete;

    void Init();

    // false = no valid recent measurement (never read, or stale > 30 s).
    // Smoothed (see DefaultFilterTauS) when the answer comes from the sensor;
    // an external value is passed through untouched.
    bool GetRoomTemperature(float &celsius) const;

    /// Relative humidity from the same sensor, same cadence and validity rule.
    /// Cached here rather than read at the call site because the AHT20 has one
    /// sampler by design (see Loop()); it is also a second, independent witness
    /// of local self-heating — a warmed sensor reads RH low.
    bool GetRoomHumidity(float &percent) const;

    // Push a value into the external source, selecting it if it was not
    // already selected. Same thing `room external -temp` does, for a caller
    // inside the firmware (the room simulation).
    void SetExternalTemperature(float celsius);

    /// Hand the room back to the board sensor.
    void ClearExternalSource();

    /// Which source GetRoomTemperature() is currently answering from.
    RoomTempSource GetSource() const;

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

    float   lastTemp_ = 0.0f;     // the filter's output -- what consumers read
    float   rawTemp_ = 0.0f;      // the last reading as the sensor gave it
    int64_t lastReadUs_ = -1;     // esp_timer time of it; -1 = never read
    bool    lastValid_ = false;   // loop-task-only edge detector for the fault log

    float   lastHumidity_ = 0.0f;
    int64_t lastHumidityUs_ = -1;

    // Read once at Init rather than per sample: Setting::Get() goes to NVS, and
    // this is a tuning knob, not a live input. A change therefore takes effect
    // at the next boot. Zero or less turns the filter off, which is how you see
    // what it was doing.
    float filterTauS_ = DefaultFilterTauS;

    inline static FloatSetting filterTauSetting_{
        "room.tau", "Room Temp Filter s", DefaultFilterTauS };   // NVS key <= 15 chars

    // External source (guarded by mutex_). Deliberately not persisted: a reboot
    // always comes back on the real sensor, so a forgotten simulation cannot
    // survive a power cycle.
    RoomTempSource source_ = RoomTempSource::Sensor;
    float          externalTemp_ = 0.0f;
    int64_t        externalUs_ = -1;
};
