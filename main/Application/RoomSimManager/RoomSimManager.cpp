#include "RoomSimManager.h"
#include "CommandManager/CommandManager.h"
#include "RoomTemperatureManager.h"
#include "ClimateManager.h"
#include "OpenThermManager.h"
#include "SettingsManager/SettingsManager.h"
#include "JsonScope.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

// See main/CMakeLists.txt: -DROOM_SIM_FORCE_ENABLE=1 turns the simulation on
// without the setting, for a board that cannot be reached to write it.
#ifndef ROOM_SIM_FORCE_ENABLE
#define ROOM_SIM_FORCE_ENABLE 0
#endif

RoomSimManager::RoomSimManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void RoomSimManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);
    serviceProvider_.getSettingsManager().Register({
        &enableSetting_, &tauSetting_, &outdoorSetting_,
        &gainSetting_, &coolSetting_, &startSetting_, &driveSetting_ });

    {
        LOCK(mutex_);
        enabled_   = enableSetting_.Get() != 0 || ROOM_SIM_FORCE_ENABLE;
        tau_       = tauSetting_.Get() * 60.0f;
        outdoor_   = outdoorSetting_.Get();
        heatGain_  = gainSetting_.Get();
        coolGain_  = coolSetting_.Get();
        room_      = startSetting_.Get();
        drive_     = driveSetting_.Get() == 1 ? Drive::Boiler : Drive::Self;
        startedUs_ = esp_timer_get_time();
        if (tau_ < 1.0f) tau_ = 1.0f;   // a zero time constant would divide by zero
    }

    if (!enabled_)
    {
        init.SetReady();
        ESP_LOGI(TAG, "Disabled (set sim.enable=1 to simulate the room)");
        return;
    }

    // Seed the external source before the first control step runs, so the
    // thermostat never steers on the board sensor during a simulated run.
    serviceProvider_.getRoomTemperatureManager().SetExternalTemperature(room_);

    task_.Init("roomsim", 5, 4096);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGW(TAG, "SIMULATING THE ROOM: start %.1f C, outdoor %.1f C, tau %.0f min, "
                  "heat gain %.1f C, drive %s",
             room_, outdoor_, tau_ / 60.0f, heatGain_,
             drive_ == Drive::Boiler ? "boiler" : "self");
}

float RoomSimManager::DriveNow() const
{
    if (drive_ == Drive::Boiler)
    {
        OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();
        if (boiler.coolingActive) return -1.0f;
        // flame is what the gateway reports as "actually burning"; chActive is
        // its CH mode. Either counts as heat being delivered.
        if (boiler.flame || boiler.chActive) return 1.0f;
        return 0.0f;
    }

    // Self: the thermostat's own modulating demand. chEnable gates it, so a
    // satisfied room delivers nothing even if the PID output has not decayed.
    ClimateStatus status = serviceProvider_.getClimateManager().GetStatus();
    if (!status.chEnable) return 0.0f;
    float u = status.pidOutput;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    return u;
}

void RoomSimManager::Step(float dtSeconds)
{
    float u = DriveNow();

    float room, tau, outdoor, gain;
    {
        LOCK(mutex_);
        gain = u >= 0.0f ? heatGain_ : coolGain_;
        room = room_;
        tau = tau_;
        outdoor = outdoor_;
    }

    room += (u * gain - (room - outdoor)) * dtSeconds / tau;

    {
        LOCK(mutex_);
        room_ = room;
        lastDrive_ = u;
    }

    serviceProvider_.getRoomTemperatureManager().SetExternalTemperature(room);

    // The trace. This is the experiment's output: on a board with no route to
    // it, the console is the only place a run can be read from.
    ClimateStatus status = serviceProvider_.getClimateManager().GetStatus();
    OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();
    ESP_LOGI(TAG, "t=%llds room=%.2f out=%.1f sp=%.1f u=%.2f pid=%.2f tset=%.1f "
                  "ch=%d cool=%d | ot link=%d flame=%d chAct=%d",
             (long long)((esp_timer_get_time() - startedUs_) / 1000000),
             room, outdoor, status.activeSetpoint, u, status.pidOutput, status.tSet,
             status.chEnable ? 1 : 0, status.coolEnable ? 1 : 0,
             boiler.linked ? 1 : 0, boiler.flame ? 1 : 0, boiler.chActive ? 1 : 0);
}

void RoomSimManager::Loop()
{
    int64_t last = esp_timer_get_time();
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1000000.0f;
        last = now;
        Step(dt);
    }
}

RequestError RoomSimManager::Cmd_SimStatus(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());
    WriteStatus(ctx.out);
    return RequestError::Ok;
}

RequestError RoomSimManager::Cmd_SimSet(CommandContext& ctx)
{
    uint32_t enable = 0xFFFFFFFF;   // sentinel: argument absent
    uint32_t drive = 0xFFFFFFFF;
    float tau = NAN, outdoor = NAN, gain = NAN, cool = NAN, start = NAN, room = NAN;
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("enable",  enable),
        Optional("tau",     tau),
        Optional("outdoor", outdoor),
        Optional("gain",    gain),
        Optional("cool",    cool),
        Optional("start",   start),
        Optional("room",    room),
        Optional("drive",   drive)
    ));

    // Everything except `room` is a setting, because a run has to be set up on
    // a board that may have no route to it: write it, reboot, watch the console.
    if (enable != 0xFFFFFFFF) enableSetting_.Set(enable ? 1 : 0);
    if (drive  != 0xFFFFFFFF) driveSetting_.Set(drive ? 1 : 0);
    if (!std::isnan(tau))     tauSetting_.Set(tau);
    if (!std::isnan(outdoor)) outdoorSetting_.Set(outdoor);
    if (!std::isnan(gain))    gainSetting_.Set(gain);
    if (!std::isnan(cool))    coolSetting_.Set(cool);
    if (!std::isnan(start))   startSetting_.Set(start);
    serviceProvider_.getSettingsManager().Save();

    // Outdoor and the room temperature also take effect immediately, so a
    // running experiment can be stepped (open a window, teleport the room)
    // without a reboot.
    {
        LOCK(mutex_);
        if (!std::isnan(outdoor)) outdoor_ = outdoor;
        if (!std::isnan(gain))    heatGain_ = gain;
        if (!std::isnan(cool))    coolGain_ = cool;
        if (!std::isnan(tau))     tau_ = tau * 60.0f < 1.0f ? 1.0f : tau * 60.0f;
        if (!std::isnan(room))    room_ = room;
    }
    if (!std::isnan(room))
        serviceProvider_.getRoomTemperatureManager().SetExternalTemperature(room);

    WriteStatus(ctx.out);
    return RequestError::Ok;
}

void RoomSimManager::WriteStatus(Stream &out)
{
    bool enabled;
    float room, tau, outdoor, gain, cool, u;
    Drive drive;
    {
        LOCK(mutex_);
        enabled = enabled_;
        room = room_;
        tau = tau_;
        outdoor = outdoor_;
        gain = heatGain_;
        cool = coolGain_;
        u = lastDrive_;
        drive = drive_;
    }

    JsonObject resp(out);
    resp.field("enabled", enabled);
    resp.field("room", room);
    resp.field("outdoor", outdoor);
    resp.field("tauMin", tau / 60.0f);
    resp.field("heatGain", gain);
    resp.field("coolGain", cool);
    resp.field("drive", drive == Drive::Boiler ? "boiler" : "self");
    resp.field("lastDrive", u);
    // enabled reflects this boot; the setting is what the next boot will do.
    resp.field("enableSetting", enableSetting_.Get());
}
