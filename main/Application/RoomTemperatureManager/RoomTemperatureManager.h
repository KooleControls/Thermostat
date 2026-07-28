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
// touch the sensor directly — a future external/BLE source swaps in behind
// GetRoomTemperature() without touching them. Calibration is deliberately
// absent (docs/backlog/room-temp-calibration.md).
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
    void Cmd_RoomTemp(Stream &in, Stream &out);   // roomTemp

    // Shared validity rule: a reading is valid if it exists and is no older
    // than ValidityUs. Boundary: exactly ValidityUs old is still valid.
    static bool IsValid(int64_t readUs, int64_t now);

    inline static CommandEntry commands_[] = {
        { "roomTemp", &InvokeCommand<&RoomTemperatureManager::Cmd_RoomTemp> },
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
};
