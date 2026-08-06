#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"

// Owns domestic hot water: enable + setpoint (persisted), pushed to the boiler
// via OpenThermManager::SetDhwDemand. No control loop — the boiler owns tank
// heating; this just states the demand. Readout (dhwActive/dhwTemp/dhwPresent)
// is pulled live from OpenThermManager::GetState() at command time.
class HotWaterManager
{
    static constexpr const char *TAG = "HotWaterManager";
    static constexpr float kSetpointMin = 30.0f;
    static constexpr float kSetpointMax = 80.0f;

public:
    explicit HotWaterManager(ServiceProvider &serviceProvider);

    HotWaterManager(const HotWaterManager &) = delete;
    HotWaterManager &operator=(const HotWaterManager &) = delete;
    HotWaterManager(HotWaterManager &&) = delete;
    HotWaterManager &operator=(HotWaterManager &&) = delete;

    void Init();

private:
    void Push();   // send current enable+setpoint to OpenThermManager
    RequestError Cmd_HotWaterSet(CommandContext& ctx);
    RequestError Cmd_HotWaterStatus(CommandContext& ctx);
    void WriteStatus(Stream &out);

    inline static CommandEntry commands_[] = {
        { "hotwater", "set",    &InvokeCommand<&HotWaterManager::Cmd_HotWaterSet> },
        { "hotwater", "status", &InvokeCommand<&HotWaterManager::Cmd_HotWaterStatus> },
    };

    inline static BoolSetting  enableSetting_{ "dhw.enable", "DHW Enable", false };
    inline static FloatSetting setpointSetting_{ "dhw.setpoint", "DHW Setpoint", 60.0f };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    bool  enable_ = false;
    float setpoint_ = 60.0f;
};
