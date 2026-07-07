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

    // t_set clamp from the boiler (ID 49, s8/s8 upper/lower)
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
    // ClimateManager's future entry. change-detected: calling it periodically
    // with an unchanged demand is free.
    void          SetDemand(const OtDemand &d);

private:
    // Outcome of one validated exchange (see Exchange() in the .cpp).
    enum class OtResult { Ok, Fail, Unsupported, Rejected };

    void Loop();
    void ServiceLink(class OtLink &link);         // one cycle while the link is up
    void RecoverLink(class OtLink &link);         // backoff recovery while it is down
    void LogLinkTransition();

    // One OT exchange with full reply validation (parity + data-ID + ack type).
    OtResult Exchange(class OtLink &link, bool write, uint8_t id,
                      uint16_t requestValue, uint16_t &replyValue);
    OtResult Read(class OtLink &link, uint8_t id, uint16_t &value,
                  uint16_t requestValue = 0);
    OtResult Write(class OtLink &link, uint8_t id, uint16_t value);

    bool DoStatus(class OtLink &link);            // ID 0 keepalive + status decode
    void DoOverrideRead(class OtLink &link);      // ID 9 → AdoptOverride
    void DoRotationSlot(class OtLink &link);      // one slot of the rotation
    size_t NextSlot();                            // dirty-jump, unsupported-skip, advance
    void   AdvanceSlot();
    bool   GetWriteValue(uint8_t id, float &v);   // demand value for a write slot
    void   StoreRead(uint8_t id, uint16_t value); // decode a read reply into state_

    // Locked leaf helpers — take mutex_ themselves; callers hold no lock.
    uint16_t MasterStatusBits();
    void     StoreSlaveStatus(uint8_t bits);
    void     MarkLinkDown();
    void     AdoptOverride(float setpoint);

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
    bool     lastLoggedLinked_ = false;   // LogLinkTransition edge detector

    // Unsupported-ID bookkeeping for the rotation (UNKNOWN-DATAID reply → rare
    // retry). kSlots (see OpenThermManager.cpp) is currently 14.
    bool     unsupported_[/*kSlots*/ 14] = {};
    uint32_t pass_ = 0;
};
