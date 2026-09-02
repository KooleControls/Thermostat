#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Task.h"
#include "CommandManager/CommandEntry.h"
#include "TypedSettings.h"
#include <cstdint>

// A simulated room, so the control loop can be exercised end to end without a
// building attached. It owns a room temperature, pushes it into
// RoomTemperatureManager's external source, and moves it according to whatever
// heat is being delivered:
//
//     dT/dt = ( u * gain - (T - outdoor) ) / tau
//
// `gain` is the steady-state rise the heating can hold above outdoor at full
// output; `tau` the room's time constant; `u` the heat drive in [-1, 1].
//
// Two things this deliberately is not. It is not a building model -- one
// capacity, one loss path, no solar, no zones. And it is not a substitute for
// testing the PID itself: it validates structure (does the loop converge, does
// it hunt, does the deadband behave) rather than numbers, because the numbers
// depend on the model as much as on the controller.
//
// It runs on-device rather than on a PC because that needs no network: the
// experiment survives having no route to the board, and the trace comes out of
// the console.
class RoomSimManager
{
    static constexpr const char *TAG = "RoomSimManager";
    static constexpr int LoopDelayMs = 5000;   // matches the control loop's cadence

public:
    // Where the heat drive comes from.
    enum class Drive : uint32_t
    {
        // The thermostat's own PID output. Models a boiler that follows t_set
        // exactly -- so this closes the loop around the thermostat alone, and
        // says nothing about what the gateway decided.
        Self = 0,
        // The gateway's reported CH-active / flame over OpenTherm. This is the
        // faithful pair test: the room only warms when the gateway actually
        // runs the heat. Needs the gateway to report it (see the notes in the
        // commit message), otherwise the room simply never warms.
        Boiler = 1,
    };

    explicit RoomSimManager(ServiceProvider &serviceProvider);

    RoomSimManager(const RoomSimManager &) = delete;
    RoomSimManager &operator=(const RoomSimManager &) = delete;
    RoomSimManager(RoomSimManager &&) = delete;
    RoomSimManager &operator=(RoomSimManager &&) = delete;

    void Init();

private:
    void Loop();
    void Step(float dtSeconds);
    float DriveNow() const;
    RequestError Cmd_SimStatus(CommandContext& ctx);
    RequestError Cmd_SimSet(CommandContext& ctx);
    void WriteStatus(Stream &out);

    inline static CommandEntry commands_[] = {
        { "sim", "status", &InvokeCommand<&RoomSimManager::Cmd_SimStatus> },
        { "sim", "set",    &InvokeCommand<&RoomSimManager::Cmd_SimSet> },
    };

    // Settings rather than command arguments, so a run survives a reboot and
    // can be set up on a board with no route to it. Keys are <= 15 chars (NVS).
    inline static UInt32Setting enableSetting_{ "sim.enable",  "Room Sim Enable", 0 };
    inline static FloatSetting  tauSetting_{    "sim.tau",     "Room Sim Tau (min)", 15.0f };
    inline static FloatSetting  outdoorSetting_{"sim.outdoor", "Room Sim Outdoor C", 8.0f };
    inline static FloatSetting  gainSetting_{   "sim.gain",    "Room Sim Heat Gain C", 25.0f };
    inline static FloatSetting  coolSetting_{   "sim.cool",    "Room Sim Cool Gain C", 15.0f };
    inline static FloatSetting  startSetting_{  "sim.start",   "Room Sim Start C", 18.0f };
    inline static UInt32Setting driveSetting_{  "sim.drive",   "Room Sim Drive (0=self,1=boiler)", 0 };

    ServiceProvider &serviceProvider_;
    InitState initState_;
    mutable Mutex mutex_;
    Task task_;

    bool  enabled_ = false;
    float room_ = 18.0f;
    float tau_ = 900.0f;        // seconds
    float outdoor_ = 8.0f;
    float heatGain_ = 25.0f;
    float coolGain_ = 15.0f;
    Drive drive_ = Drive::Self;

    // last step, for the status reply and the trace
    float lastDrive_ = 0.0f;
    int64_t startedUs_ = 0;
};
