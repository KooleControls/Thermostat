#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include <cstdint>

// What WE demand from the boiler (thermostat = OT master). Pushed in via
// SetDemand() — bench: the otSet command; later: ClimateManager's PID.
struct OtDemand
{
    bool  chEnable     = false;   // ID 0 master bit 0 — heat demand
    bool  dhwEnable    = false;   // ID 0 master bit 1
    bool  coolEnable   = false;   // ID 0 master bit 2 — cooling demand
    float roomSetpoint = 20.0f;   // °C → ID 16 (adopted from ID 9 on override)
    float dhwSetpoint  = 60.0f;   // °C → ID 56
    float tSet         = 0.0f;    // °C → ID 1 (later: the PID output)
};

// What the boiler/gateway reports back. Single source of boiler truth;
// consumers pull snapshots via GetState().
struct OtBoilerState
{
    bool linked = false;                 // recent Status exchange succeeded

    // slave status bits (ID 0 reply, low byte)
    bool fault = false;
    bool chActive = false;
    bool dhwActive = false;
    bool flame = false;
    bool coolingActive = false;

    // slave config (ID 3 high byte) — capabilities
    bool dhwPresent = false;
    bool coolingSupported = false;

    // sensors (f8.8 unless noted)
    float boilerTemp = 0;                // ID 25
    float returnTemp = 0;                // ID 28
    float dhwTemp = 0;                   // ID 26
    float modulation = 0;                // ID 17 (%)
    float chPressure = 0;                // ID 18 (bar)
    float outsideTemp = 0;               // ID 27

    // diagnostics (u16)
    uint16_t oemFaultCode = 0;           // ID 5
    uint16_t oemDiagCode  = 0;           // ID 115

    // t_set clamp from the boiler (ID 57, s8/s8 upper/lower)
    float maxTSetUpper = 80;
    float maxTSetLower = 30;
};

// OpenTherm MASTER (gateway = slave/boiler emulator). 500 ms cycle:
// Status keepalive + ID 9 override read every other cycle + one slot of a
// fixed write/read rotation. See docs/superpowers/specs/
// 2026-07-06-opentherm-link-manager-design.md for the normative schedule.
class OpenThermManager
{
    static constexpr const char *TAG = "OpenThermManager";
    static constexpr int  LoopDelayMs   = 500;
    static constexpr int  LinkFailLimit = 6;      // ~3 s → linked=false
    static constexpr int  RetryUnsupportedEvery = 120;  // rotation passes

public:
    explicit OpenThermManager(ServiceProvider &serviceProvider);

    OpenThermManager(const OpenThermManager &) = delete;
    OpenThermManager &operator=(const OpenThermManager &) = delete;
    OpenThermManager(OpenThermManager &&) = delete;
    OpenThermManager &operator=(OpenThermManager &&) = delete;

    void Init();

    OtBoilerState GetState() const;
    OtDemand      GetDemand() const;
    void          SetDemand(const OtDemand &d);   // ClimateManager's future entry

private:
    void Loop();
    bool DoStatus(class OtLink &link);            // ID 0 exchange + state update
    void DoOverrideRead(class OtLink &link);      // ID 9
    void DoRotationSlot(class OtLink &link);      // one write/read slot
    void Cmd_Status(Stream &in, Stream &out);     // otStatus
    void Cmd_Set(Stream &in, Stream &out);        // otSet

    inline static CommandEntry commands_[] = {
        { "otStatus", &InvokeCommand<&OpenThermManager::Cmd_Status> },
        { "otSet",    &InvokeCommand<&OpenThermManager::Cmd_Set> },
    };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    OtDemand      demand_;
    OtBoilerState state_;
    bool demandDirty_ = false;    // set by SetDemand/otSet → jump the rotation

    uint32_t cycle_ = 0;
    size_t   slot_ = 0;
    int      failStreak_ = 0;
    int      recoverBackoffS_ = 5;
    int64_t  nextRecoverUs_ = 0;
};
