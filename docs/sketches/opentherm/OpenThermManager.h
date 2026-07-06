#pragma once
#include <cstdint>

// ══════════════════════════════════════════════════════════════
// DESIGN EXAMPLE (not compiled) — proposed
// Application/OpenThermManager/OpenThermManager.h
// ══════════════════════════════════════════════════════════════
//
// OpenTherm MASTER for the thermostat (gateway = OT slave / boiler
// emulator). A 500 ms task keeps Status (ID 0, the <1 s keepalive that
// carries the CH / DHW / cooling enable bits) flowing, plus one rotating
// secondary message per cycle:
//
//   writes (on change + periodic refresh):
//     ID 1  t_set         ID 16 room setpoint
//     ID 24 room temp     ID 56 DHW setpoint
//   reads (slow rotation; UNKNOWN-DATAID -> mark unsupported, rare retry):
//     ID 17 modulation    ID 25 boiler temp   ID 26 DHW temp
//     ID 28 return temp   ID 18 CH pressure   ID 27 outside temp
//     ID 5  OEM fault     ID 115 OEM diag     ID 57 max t_set bounds
//     ID 3  slave config (DHW present / cooling supported)
//   override (every ~1 s):
//     ID 9  remote-override room setpoint — nonzero & != current setpoint
//           -> adopt into demand.roomSetpoint (logged); ID 16 echoes it
//           next cycle, which is exactly what the gateway's override
//           logic waits for. A later local change simply overwrites.
//
// Demand sources until ClimateManager (backlog item 5) exists:
//   • room temp (ID 24): read from getBoard().GetTemperatureSensor()
//     each cycle — documented as temporary; item 5 takes ownership.
//   • setpoint / enables: bench-driven via the command table below
//     (web-UI console). ClimateManager later calls SetDemand() instead.
//
// Link supervision: N consecutive Transaction failures -> linked=false
// (enables stop flowing — safe state falls out naturally), then
// OtLink::Recover() with backoff; state transitions logged once.

// ── What WE demand from the boiler ────────────────────────────
struct OtDemand
{
    bool  chEnable     = false;   // ID 0 master bit — heat demand
    bool  dhwEnable    = false;   // ID 0 master bit
    bool  coolEnable   = false;   // ID 0 master bit — cooling demand
    float roomSetpoint = 20.0f;   // °C → ID 16 (adopted from ID 9 on override)
    float dhwSetpoint  = 60.0f;   // °C → ID 56
    float tSet         = 0.0f;    // °C → ID 1 (later: the PID output)
};

// ── What the boiler/gateway reports back ──────────────────────
struct OtBoilerState
{
    bool linked = false;                    // recent Status response received

    // slave status bits (ID 0 reply)
    bool fault = false;
    bool chActive = false;
    bool dhwActive = false;
    bool flame = false;
    bool coolingActive = false;

    // slave config / capabilities (ID 3)
    bool dhwPresent = false;
    bool coolingSupported = false;          // gates offering Cool mode (UI, item 7)

    // sensors
    float boilerTemp = 0;                   // ID 25
    float returnTemp = 0;                   // ID 28
    float dhwTemp = 0;                      // ID 26
    float modulation = 0;                   // ID 17 (%)
    float chPressure = 0;                   // ID 18 (bar)
    float outsideTemp = 0;                  // ID 27

    // diagnostics
    uint16_t oemFaultCode = 0;              // ID 5 (low byte = app-specific flags)
    uint16_t oemDiagCode  = 0;              // ID 115

    // bounds from the boiler (ID 57) — clamp for tSet
    float maxTSetUpper = 80;
    float maxTSetLower = 30;
};

// ── The manager ───────────────────────────────────────────────
class OpenThermManager
{
public:
    explicit OpenThermManager(ServiceProvider &serviceProvider);
    void Init();                          // handshake done by Board; starts task

    OtBoilerState GetState() const;       // thread-safe snapshot (UI/web/Climate)
    OtDemand      GetDemand() const;
    void SetDemand(const OtDemand &d);    // ClimateManager's future entry point

private:
    void Loop();                          // the 500 ms master cycle

    // Bench commands (web-UI console / WebSocket), registered in Init():
    //   otStatus                          -> JSON dump of OtBoilerState+OtDemand
    //   otSet {setpoint,ch,dhw,cool,dhwSetpoint,tset}  -> partial demand update
    inline static CommandEntry commands_[] = { /* otStatus, otSet */ };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    OtDemand      demand_;
    OtBoilerState state_;
};

// Also part of this design (not sketched here):
//   • OtFrame.h (manager-local): 32-bit frame build/parse — parity bit,
//     msg-type, data-ID, f8.8 value encoding. ~40 lines.
//   • drivers/MockOtLink.h: Ready()=false, Transaction()=false, Recover()=false.
//   • diyless Board: owns Stm32OpenThermLink, GetOtLink() → OtLink&;
//     devkit Board: owns MockOtLink, same accessor.
