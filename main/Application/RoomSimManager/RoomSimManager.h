#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include "ClimateMode.h"
#include <cstdint>

// A simulated room, so the control loop can be exercised end to end without a
// building attached.
//
// Three thermal nodes, because a one-node room cannot produce the behaviour
// that matters:
//
//   water  the emitter. Lags the commanded t_set, and keeps giving heat after
//          the thermostat stops asking -- which is where overshoot comes from.
//          Output follows the radiator law, ~dT^1.3, so a low t_set delivers
//          far less than a linear reading of the PID output suggests.
//   air    what the thermostat measures. Fast: tens of minutes.
//   mass   floor, walls, furniture. Slow: many hours. Over one heating cycle it
//          is a drifting bias rather than a dynamic, and that bias is why a
//          room that has been warm all day behaves unlike one warmed from cold.
//
//   Cair dTair/dt  = Qemit + Qcool + Qgain + (Tmass-Tair)*Kmass + (Tout-Tair)*Kloss
//   Cmass dTmass/dt = (Tair-Tmass)*Kmass
//   Twater -> t_set when CH is enabled, else decays toward Tair
//   Qemit  = Prad * (max(0, Twater-Tair)/50)^1.3
//
// Outdoor temperature follows a diurnal sine, and occupancy gains a daily
// profile, so the run is not a step response into a constant world.
//
// It runs on-device rather than on a PC because that needs no network: the
// experiment survives a board with no route to it, and the trace comes out of
// the console. The scenario is compiled in for the same reason -- a run that
// changes mode has to be able to change it without anyone reachable to ask.
class RoomSimManager
{
    static constexpr const char *TAG = "RoomSimManager";
    static constexpr int LoopDelayMs = 5000;    // matches the control loop's cadence
    static constexpr int TraceEvery = 3;        // log every 3rd step: 15 s, ample against a 15 min air tau
    static constexpr float WaterTauS = 240.0f;  // emitter lag, ~4 min
    static constexpr float RadRefDeltaT = 50.0f;  // radiator rating condition
    static constexpr float RadExponent = 1.3f;

public:
    // Where the heat drive comes from.
    enum class Drive : uint32_t
    {
        // The thermostat's own t_set and CH/cool enables. Models a boiler that
        // follows t_set faithfully, so this closes the loop around the
        // thermostat and says nothing about what the gateway decided.
        Self = 0,
        // The gateway's reported flame / CH-active over OpenTherm: the room
        // only warms when the gateway actually runs the heat. The faithful pair
        // test, but it needs the gateway to report that, which today it only
        // does from real HVAC master units.
        Boiler = 1,
    };

    // One scheduled change. The scenario is a compiled-in table because the
    // panel cannot be pressed and the command surface cannot be reached.
    struct Event
    {
        int32_t atSeconds;
        enum class Kind : uint8_t { Mode, Setpoint, OutdoorOffset, End } kind;
        float value;          // Mode: a ClimateMode. Setpoint/Offset: degrees.
        const char *note;
    };

    explicit RoomSimManager(ServiceProvider &serviceProvider);

    RoomSimManager(const RoomSimManager &) = delete;
    RoomSimManager &operator=(const RoomSimManager &) = delete;
    RoomSimManager(RoomSimManager &&) = delete;
    RoomSimManager &operator=(RoomSimManager &&) = delete;

    void Init();

private:
    void Loop();
    void Step(float dtSeconds, int32_t elapsedS);
    void ApplyDueEvents(int32_t elapsedS);
    float OutdoorAt(int32_t elapsedS) const;
    float GainAt(int32_t elapsedS) const;
    RequestError Cmd_SimStatus(CommandContext& ctx);
    RequestError Cmd_SimSet(CommandContext& ctx);
    void WriteStatus(Stream &out);

    inline static CommandEntry commands_[] = {
        { "sim", "status", &InvokeCommand<&RoomSimManager::Cmd_SimStatus> },
        { "sim", "set",    &InvokeCommand<&RoomSimManager::Cmd_SimSet> },
    };

    // Settings rather than command arguments: a run has to be settable on a
    // board that may have no route to it. Keys are <= 15 chars (NVS).
    inline static UInt32Setting enableSetting_{ "sim.enable",  "Room Sim Enable", 0 };
    inline static UInt32Setting driveSetting_{  "sim.drive",   "Room Sim Drive (0=self,1=boiler)", 0 };
    inline static UInt32Setting scenarioSetting_{ "sim.scen",  "Room Sim Scenario (0=off,1=modes)", 1 };
    inline static UInt32Setting loopSetting_{   "sim.loop",    "Room Sim Loop Scenario", 1 };
    inline static FloatSetting  startSetting_{  "sim.start",   "Room Sim Start Air C", 17.0f };
    // Capacities in kJ/K, conductances in W/K -- a small, lightly furnished,
    // well-insulated room. Air node time constant works out near 15 minutes,
    // which is at the brisk end of real but keeps a phase observable.
    inline static FloatSetting  cAirSetting_{   "sim.cair",    "Room Sim Air kJ/K", 58.0f };
    inline static FloatSetting  cMassSetting_{  "sim.cmass",   "Room Sim Mass kJ/K", 2000.0f };
    inline static FloatSetting  lossSetting_{   "sim.loss",    "Room Sim Loss W/K", 25.0f };
    inline static FloatSetting  couplSetting_{  "sim.coupl",   "Room Sim Air-Mass W/K", 40.0f };
    inline static FloatSetting  radSetting_{    "sim.rad",     "Room Sim Radiator W at dT50", 1000.0f };
    inline static FloatSetting  coolSetting_{   "sim.coolw",   "Room Sim Cooling W", 1200.0f };
    inline static FloatSetting  outMeanSetting_{"sim.outmean", "Room Sim Outdoor Mean C", 5.0f };
    inline static FloatSetting  outSwingSetting_{"sim.outsw",  "Room Sim Outdoor Swing K", 4.0f };
    inline static FloatSetting  dayLenSetting_{ "sim.daylen",  "Room Sim Day Length min", 180.0f };
    inline static FloatSetting  gainSetting_{   "sim.gainw",   "Room Sim Occupancy Gain W", 150.0f };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    bool  enabled_ = false;
    bool  scenario_ = true;
    Drive drive_ = Drive::Self;

    // thermal state
    float tAir_ = 17.0f;
    float tMass_ = 17.0f;
    float tWater_ = 17.0f;

    // parameters, unpacked from settings at Init
    float cAirJ_ = 58000.0f;
    float cMassJ_ = 2000000.0f;
    float lossWk_ = 25.0f;
    float couplWk_ = 40.0f;
    float radW_ = 2000.0f;
    float coolW_ = 1500.0f;
    float outMean_ = 5.0f;
    float outSwing_ = 4.0f;
    float dayLenS_ = 10800.0f;
    float gainW_ = 150.0f;
    float outOffset_ = 0.0f;      // stepped by the scenario: a window, a cold snap

    // last step, for the status reply
    float lastQEmit_ = 0.0f;
    float lastQCool_ = 0.0f;
    float lastOutdoor_ = 5.0f;
    size_t nextEvent_ = 0;
    bool loop_ = true;
    int32_t lastCycleS_ = 0;
    uint32_t cycleCount_ = 0;
    uint32_t stepCount_ = 0;
    int64_t startedUs_ = 0;
};
