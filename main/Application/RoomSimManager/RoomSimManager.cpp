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

namespace {

// The run. Each mode gets a phase long enough for the air node (~15 min) to
// actually go somewhere, and the phases are ordered so each one starts from a
// room that makes its mode meaningful: Heat from cold, Cool from the warmth
// Heat left behind, Auto from whatever Cool ended at.
constexpr RoomSimManager::Event kScenario[] = {
    {   60, RoomSimManager::Event::Kind::Setpoint,      21.0f, "heat to 21" },
    {   60, RoomSimManager::Event::Kind::Mode,   (float)(int)ClimateMode::Heat, "HEAT phase" },
    { 1800, RoomSimManager::Event::Kind::Setpoint,      23.0f, "setpoint step 21->23" },
    { 2700, RoomSimManager::Event::Kind::OutdoorOffset, -8.0f, "cold snap / window open" },
    { 3300, RoomSimManager::Event::Kind::OutdoorOffset,   0.0f, "window shut" },
    { 3600, RoomSimManager::Event::Kind::Setpoint,      19.0f, "cool to 19" },
    { 3600, RoomSimManager::Event::Kind::Mode,   (float)(int)ClimateMode::Cool, "COOL phase" },
    { 5100, RoomSimManager::Event::Kind::Setpoint,      21.0f, "auto at 21" },
    { 5100, RoomSimManager::Event::Kind::Mode,   (float)(int)ClimateMode::Auto, "AUTO phase" },
    { 7200, RoomSimManager::Event::Kind::End,            0.0f, "cycle complete" },
};
constexpr size_t kScenarioCount = sizeof(kScenario) / sizeof(kScenario[0]);
// The End event's time is the cycle length, so the table stays the only place
// the schedule is written down.
constexpr int32_t kCycleSeconds = kScenario[kScenarioCount - 1].atSeconds;

}   // namespace

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
        &enableSetting_, &driveSetting_, &scenarioSetting_, &startSetting_,
        &cAirSetting_, &cMassSetting_, &lossSetting_, &couplSetting_,
        &radSetting_, &coolSetting_, &outMeanSetting_, &outSwingSetting_,
        &dayLenSetting_, &gainSetting_ });

    {
        LOCK(mutex_);
        enabled_  = enableSetting_.Get() != 0 || ROOM_SIM_FORCE_ENABLE;
        drive_    = driveSetting_.Get() == 1 ? Drive::Boiler : Drive::Self;
        scenario_ = scenarioSetting_.Get() != 0;
        loop_     = loopSetting_.Get() != 0;

        cAirJ_    = cAirSetting_.Get() * 1000.0f;
        cMassJ_   = cMassSetting_.Get() * 1000.0f;
        lossWk_   = lossSetting_.Get();
        couplWk_  = couplSetting_.Get();
        radW_     = radSetting_.Get();
        coolW_    = coolSetting_.Get();
        outMean_  = outMeanSetting_.Get();
        outSwing_ = outSwingSetting_.Get();
        dayLenS_  = dayLenSetting_.Get() * 60.0f;
        gainW_    = gainSetting_.Get();

        if (cAirJ_  < 1000.0f)  cAirJ_  = 1000.0f;    // guard the divisions
        if (cMassJ_ < 1000.0f)  cMassJ_ = 1000.0f;
        if (dayLenS_ < 60.0f)   dayLenS_ = 60.0f;

        tAir_ = startSetting_.Get();
        // The fabric starts in equilibrium with the air rather than at the same
        // temperature as the outdoors: a room being tested has been standing,
        // not just built.
        tMass_ = tAir_;
        tWater_ = tAir_;
        lastOutdoor_ = outMean_;
        nextEvent_ = 0;
        lastCycleS_ = 0;
        cycleCount_ = 0;
        startedUs_ = esp_timer_get_time();
    }

    if (!enabled_)
    {
        init.SetReady();
        ESP_LOGI(TAG, "Disabled (set sim.enable=1 to simulate the room)");
        return;
    }

    serviceProvider_.getRoomTemperatureManager().SetExternalTemperature(tAir_);

    task_.Init("roomsim", 5, 8192);   // the trace line is float-heavy; 4096 left little headroom
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    float airTauMin = cAirJ_ / (lossWk_ + couplWk_) / 60.0f;
    ESP_LOGW(TAG, "SIMULATING THE ROOM: air %.1f C, mass %.1f C, air tau %.0f min, "
                  "loss %.0f W/K, rad %.0f W, cool %.0f W, drive %s, scenario %s",
             tAir_, tMass_, airTauMin, lossWk_, radW_, coolW_,
             drive_ == Drive::Boiler ? "boiler" : "self",
             scenario_ ? (loop_ ? "looping" : "once") : "off");
    if (scenario_ && loop_)
        ESP_LOGW(TAG, "Scenario cycle is %d min (heat, setpoint step, cold snap, "
                      "cool, auto) and repeats until the board is reflashed",
                 (int)(kCycleSeconds / 60));
}

float RoomSimManager::OutdoorAt(int32_t elapsedS) const
{
    // Diurnal sine: coldest at the start, warmest half a "day" later, so a run
    // is not a step response into a constant world.
    float phase = 2.0f * (float)M_PI * (float)elapsedS / dayLenS_;
    return outMean_ - outSwing_ * cosf(phase) + outOffset_;
}

float RoomSimManager::GainAt(int32_t elapsedS) const
{
    // Occupancy and solar, shaped over the same day: near zero at night, full
    // around the middle. Never negative.
    float phase = 2.0f * (float)M_PI * (float)elapsedS / dayLenS_;
    float shape = 0.5f - 0.5f * cosf(phase);
    return gainW_ * shape;
}

void RoomSimManager::ApplyDueEvents(int32_t elapsedS)
{
    if (!scenario_) return;

    // The scenario runs on its own clock so it can repeat, while the outdoor
    // sine and the occupancy gain keep using absolute time -- the simulated day
    // should carry on across a cycle boundary rather than restarting with it.
    int32_t cycleS = loop_ ? (elapsedS % kCycleSeconds) : elapsedS;
    if (cycleS < lastCycleS_)
    {
        nextEvent_ = 0;
        cycleCount_++;
        ESP_LOGW(TAG, "=== cycle %u complete, starting again (t=%ds absolute) ===",
                 (unsigned)cycleCount_, (int)elapsedS);
    }
    lastCycleS_ = cycleS;

    while (nextEvent_ < kScenarioCount && kScenario[nextEvent_].atSeconds <= cycleS)
    {
        const Event &e = kScenario[nextEvent_++];
        switch (e.kind)
        {
        case Event::Kind::Mode:
        {
            ClimateMode m = (ClimateMode)(int)e.value;
            serviceProvider_.getClimateManager().SetMode(m);
            ESP_LOGW(TAG, "=== t=%ds SCENARIO: %s (mode %s) ===",
                     (int)elapsedS, e.note, ClimateModeName(m));
            break;
        }
        case Event::Kind::Setpoint:
            serviceProvider_.getClimateManager().SetUserSetpoint(e.value);
            ESP_LOGW(TAG, "=== t=%ds SCENARIO: %s (setpoint %.1f) ===",
                     (int)elapsedS, e.note, e.value);
            break;

        case Event::Kind::OutdoorOffset:
            { LOCK(mutex_); outOffset_ = e.value; }
            ESP_LOGW(TAG, "=== t=%ds SCENARIO: %s (outdoor offset %+.1f) ===",
                     (int)elapsedS, e.note, e.value);
            break;

        case Event::Kind::End:
            if (!loop_)
                ESP_LOGW(TAG, "=== t=%ds SCENARIO: %s — holding, room stays "
                              "simulated ===", (int)elapsedS, e.note);
            break;
        }
    }
}

void RoomSimManager::Step(float dt, int32_t elapsedS)
{
    ClimateStatus status = serviceProvider_.getClimateManager().GetStatus();
    OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();

    // What the plant is being told to do.
    bool  chOn = false;
    bool  coolOn = false;
    float waterTarget = 0.0f;
    if (drive_ == Drive::Boiler)
    {
        chOn = boiler.flame || boiler.chActive;
        coolOn = boiler.coolingActive;
        // The gateway does not tell us a water temperature, so assume it runs
        // the emitter at what we asked for.
        waterTarget = status.tSet > 0.0f ? status.tSet : 0.0f;
    }
    else
    {
        chOn = status.chEnable && status.tSet > 0.0f;
        coolOn = status.coolEnable;
        waterTarget = status.tSet;
    }

    float tAir, tMass, tWater, cAir, cMass, loss, coupl, rad, coolW;
    {
        LOCK(mutex_);
        tAir = tAir_; tMass = tMass_; tWater = tWater_;
        cAir = cAirJ_; cMass = cMassJ_; loss = lossWk_; coupl = couplWk_;
        rad = radW_; coolW = coolW_;
    }

    float outdoor = OutdoorAt(elapsedS);
    float gain = GainAt(elapsedS);

    // Emitter water: chases t_set while the boiler is firing, otherwise gives
    // its stored heat to the room. This lag is what makes the room overshoot
    // after the thermostat has already stopped asking.
    float target = chOn ? waterTarget : tAir;
    tWater += (target - tWater) * dt / WaterTauS;

    // Radiator law. A 40 C flow into a 20 C room delivers far less than
    // "half of nominal" -- (20/50)^1.3 is about 0.30.
    float dTemit = tWater - tAir;
    float qEmit = 0.0f;
    if (dTemit > 0.0f)
        qEmit = rad * powf(dTemit / RadRefDeltaT, RadExponent);

    float qCool = coolOn ? -coolW : 0.0f;
    float qLoss = (outdoor - tAir) * loss;
    float qMass = (tMass - tAir) * coupl;

    tAir  += (qEmit + qCool + qLoss + qMass + gain) * dt / cAir;
    tMass += (tAir - tMass) * coupl * dt / cMass;

    {
        LOCK(mutex_);
        tAir_ = tAir; tMass_ = tMass; tWater_ = tWater;
        lastQEmit_ = qEmit; lastQCool_ = qCool; lastOutdoor_ = outdoor;
    }

    serviceProvider_.getRoomTemperatureManager().SetExternalTemperature(tAir);

    // The trace. On a board with no route to it, the console is the only place
    // a run can be read from, so everything needed to interpret a phase is on
    // one line.
    if (++stepCount_ % TraceEvery != 1)
        return;
    ESP_LOGI(TAG, "t=%4ds air=%5.2f mass=%5.2f water=%5.2f out=%5.2f | mode=%-4s "
                  "sp=%4.1f pid=%4.2f tset=%4.1f ch=%d cl=%d ovr=%d | "
                  "Qe=%5.0f Qc=%5.0f Ql=%5.0f Qg=%3.0f | ot=%d fl=%d",
             (int)elapsedS, tAir, tMass, tWater, outdoor,
             ClimateModeName(serviceProvider_.getClimateManager().GetMode()),
             status.activeSetpoint, status.pidOutput, status.tSet,
             status.chEnable ? 1 : 0, status.coolEnable ? 1 : 0,
             status.overrideActive ? 1 : 0,
             qEmit, qCool, qLoss, gain,
             boiler.linked ? 1 : 0, boiler.flame ? 1 : 0);
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
        int32_t elapsed = (int32_t)((now - startedUs_) / 1000000);

        ApplyDueEvents(elapsed);
        Step(dt, elapsed);
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
    uint32_t scen = 0xFFFFFFFF;
    float start = NAN, air = NAN, outdoor = NAN;
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("enable",   enable),
        Optional("drive",    drive),
        Optional("scenario", scen),
        Optional("start",    start),
        Optional("air",      air),
        Optional("outdoor",  outdoor)
    ));

    // Settings, because a run has to be set up on a board that may have no
    // route to it: write them, reboot, read the console.
    if (enable != 0xFFFFFFFF) enableSetting_.Set(enable ? 1 : 0);
    if (drive  != 0xFFFFFFFF) driveSetting_.Set(drive ? 1 : 0);
    if (scen   != 0xFFFFFFFF) scenarioSetting_.Set(scen ? 1 : 0);
    if (!std::isnan(start))   startSetting_.Set(start);
    if (!std::isnan(outdoor)) outMeanSetting_.Set(outdoor);
    serviceProvider_.getSettingsManager().Save();

    // `air` teleports the running room, for probing a phase without waiting.
    if (!std::isnan(air))
    {
        { LOCK(mutex_); tAir_ = air; }
        serviceProvider_.getRoomTemperatureManager().SetExternalTemperature(air);
    }
    if (!std::isnan(outdoor)) { LOCK(mutex_); outMean_ = outdoor; }

    WriteStatus(ctx.out);
    return RequestError::Ok;
}

void RoomSimManager::WriteStatus(Stream &out)
{
    bool enabled, scenario;
    float air, mass, water, outdoor, qe, qc;
    Drive drive;
    {
        LOCK(mutex_);
        enabled = enabled_;
        scenario = scenario_;
        air = tAir_; mass = tMass_; water = tWater_;
        outdoor = lastOutdoor_; qe = lastQEmit_; qc = lastQCool_;
        drive = drive_;
    }

    JsonObject resp(out);
    resp.field("enabled", enabled);
    resp.field("scenario", scenario);
    resp.field("airTemp", air);
    resp.field("massTemp", mass);
    resp.field("waterTemp", water);
    resp.field("outdoor", outdoor);
    resp.field("emitW", qe);
    resp.field("coolW", qc);
    resp.field("drive", drive == Drive::Boiler ? "boiler" : "self");
    resp.field("elapsedS", (uint32_t)((esp_timer_get_time() - startedUs_) / 1000000));
    resp.field("cycleSeconds", (uint32_t)kCycleSeconds);
    resp.field("cyclesDone", cycleCount_);
}
