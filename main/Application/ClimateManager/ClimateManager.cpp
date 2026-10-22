#include "ClimateManager.h"
#include "CommandManager/CommandManager.h"
#include "OpenThermManager.h"
#include "RoomTemperatureManager.h"
#include "SettingsManager/SettingsManager.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

ClimateManager::ClimateManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void ClimateManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);
    serviceProvider_.getSettingsManager().Register({ &modeSetting_, &setpointSetting_ });

    {
        LOCK(mutex_);
        mode_ = (ClimateMode)modeSetting_.Get();
        userSetpoint_ = setpointSetting_.Get();
    }

    task_.Init("climate", 5, 8192);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (mode %s, setpoint %.1f)",
             ClimateModeName(mode_), userSetpoint_);
}

void ClimateManager::Loop()
{
    while (true)
    {
        ControlStep();
        MaybeCommitSettings();   // outside ControlStep: it early-returns on sensor fault
        vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
    }
}

float ClimateManager::GetUserSetpoint() const
{
    LOCK(mutex_);
    return userSetpoint_;
}

void ClimateManager::NudgeSetpoint(float deltaC)
{
    LOCK(mutex_);
    float sp = userSetpoint_ + deltaC;
    if (sp < kSetpointMin) sp = kSetpointMin;
    if (sp > kSetpointMax) sp = kSetpointMax;
    if (fabsf(sp - userSetpoint_) < 0.001f) return;   // clamped, no change
    userSetpoint_ = sp;
    settingsDirty_ = true;
    lastChangeUs_ = esp_timer_get_time();
    // Takes effect on the next ControlStep; persisted later by MaybeCommitSettings.
}

void ClimateManager::SetUserSetpoint(float celsius)
{
    LOCK(mutex_);
    if (celsius < kSetpointMin) celsius = kSetpointMin;
    if (celsius > kSetpointMax) celsius = kSetpointMax;
    if (fabsf(celsius - userSetpoint_) < 0.001f) return;
    userSetpoint_ = celsius;
    settingsDirty_ = true;
    lastChangeUs_ = esp_timer_get_time();
}

ClimateMode ClimateManager::GetMode() const
{
    ClimateMode picked;
    { LOCK(mutex_); picked = mode_; }
    return EffectiveMode(picked, serviceProvider_.getOpenThermManager().GetState());
}

// The mode in force. The guest's pick holds only while the plant lets the
// thermostat switch between heating and cooling (OT ID 3 bit 7). When the
// plant keeps that choice itself, the thermostat runs in Auto — reporting the
// room's needs in both directions, as the Honeywell does there — and the
// stored pick waits for the choice to come back.
ClimateMode ClimateManager::EffectiveMode(ClimateMode picked, const OtBoilerState &boiler)
{
    return boiler.heatCoolByMaster ? picked : ClimateMode::Auto;
}

void ClimateManager::SetMode(ClimateMode mode)
{
    LOCK(mutex_);
    if (mode == mode_) return;
    mode_ = mode;
    settingsDirty_ = true;
    lastChangeUs_ = esp_timer_get_time();
    // Takes effect on the next ControlStep; persisted later by MaybeCommitSettings.
}

ClimateStatus ClimateManager::GetStatus() const
{
    LOCK(mutex_);
    return status_;
}

void ClimateManager::MaybeCommitSettings()
{
    ClimateMode mode;
    float setpoint;
    {
        LOCK(mutex_);
        if (!settingsDirty_) return;
        if (esp_timer_get_time() - lastChangeUs_ < kCommitIdleUs) return;   // still being adjusted
        mode = mode_;
        setpoint = userSetpoint_;
        settingsDirty_ = false;   // a change during the write below re-dirties → committed next tick
    }
    // Flash work outside the lock. One coalesced write per quiet period.
    modeSetting_.Set((uint32_t)mode);
    setpointSetting_.Set(setpoint);
    serviceProvider_.getSettingsManager().Save();
}

void ClimateManager::ControlStep()
{
    int64_t now = esp_timer_get_time();
    float dt = (lastStepUs_ < 0) ? (LoopDelayMs / 1000.0f)
                                 : (now - lastStepUs_) / 1000000.0f;
    lastStepUs_ = now;

    float room = 0;
    bool  roomValid = serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(room);
    OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();

    ClimateMode mode;
    float userSp;
    { LOCK(mutex_); mode = mode_; userSp = userSetpoint_; }
    mode = EffectiveMode(mode, boiler);

    if (!roomValid)
    {
        if (!lastSafe_)
        {
            ESP_LOGW(TAG, "Room temp invalid - safe state (no demand)");
            lastSafe_ = true;
        }
        pid_.Reset();
        PushSafeState();
        return;
    }
    if (lastSafe_)
    {
        ESP_LOGI(TAG, "Room temp valid - resuming control");
        lastSafe_ = false;
    }

    // Adopt each remote override (OT ID 9) once, as our own setpoint (RAM only —
    // deliberately NOT persisted): the display follows it and we stop
    // re-requesting the pre-override value, so the gateway sees its cap
    // satisfied and stops re-capping — otherwise ID 16 oscillates. The user
    // can still nudge away from it afterwards; on reboot we boot to the stored
    // value and the gateway re-caps, so it self-heals.
    //
    // It comes from the latch, not the raw value: the KC module sends an
    // override as a ~3 s pulse, and sampling the raw value once a step missed
    // two in five — and an old gateway never re-sends a missed one.
    bool overrideActive = false;
    if (boiler.overrideSeq != overrideSeen_)
    {
        overrideSeen_ = boiler.overrideSeq;
        if (mode != ClimateMode::Off)
        {
            float ov = boiler.overrideLatched;
            if (ov < kSetpointMin) ov = kSetpointMin;
            if (ov > kSetpointMax) ov = kSetpointMax;
            { LOCK(mutex_); userSetpoint_ = ov; userSp = ov; }
            overrideActive = true;
            ESP_LOGI(TAG, "Remote override adopted: setpoint %.1f", ov);
        }
    }
    if (mode != ClimateMode::Off && boiler.overrideSetpoint > 0.0f)
        overrideActive = true;

    float activeSp = (mode == ClimateMode::Off) ? kFrostSetpointC : userSp;

    // Over OpenTherm the mode can only be said one way: by which requests go on
    // the wire. A boiler never learns which tile the guest pressed, so a heat
    // request sent in Cool fires it. So each mode requests only its own
    // direction, as the thermostat this replaces does:
    //
    //   Heat  heating only
    //   Cool  cooling only
    //   Off   heating only, against the frost setpoint
    //   Auto  both, because the changeover is the gateway's call — but never
    //         both at once: a room cannot need heat and cooling together.
    //
    // Changeover delays and minimum on/off times stay with the gateway.
    bool heatAllowed = mode != ClimateMode::Cool;
    bool coolAllowed = boiler.coolingSupported &&
                       (mode == ClimateMode::Cool || mode == ClimateMode::Auto);

    bool  cool   = coolAllowed && (room > activeSp + 0.5f);
    float output = 0.0f;
    float tset   = 0.0f;
    if (heatAllowed && !cool)
    {
        output = pid_.Update(activeSp, room, dt);
        tset   = OutputToTSet(output, boiler.maxTSetLower, boiler.maxTSetUpper);
    }
    else
    {
        // Not heating, so the controller must not remember heating: its
        // averaging window holds up to 75 s of old output, and that is what
        // kept a heat request on beside a cooling request.
        pid_.Reset();
    }
    bool ch = tset > 0.0f;

    serviceProvider_.getOpenThermManager().SetHeatingDemand(ch, cool, activeSp, tset);

    LOCK(mutex_);
    status_ = { room, true, activeSp, output, tset, ch, cool, overrideActive };
}

float ClimateManager::OutputToTSet(float output, float lo, float hi)
{
    if (lo <= 0.0f) lo = 30.0f;
    if (hi <= lo)   hi = 80.0f;
    if (output <= 0.01f) return 0.0f;
    float t = lo + output * (hi - lo);
    if (t < lo) t = lo;
    if (t > hi) t = hi;
    return t;
}

void ClimateManager::PushSafeState()
{
    serviceProvider_.getOpenThermManager().SetHeatingDemand(false, false, 0.0f, 0.0f);
    LOCK(mutex_);
    // roomValid=false is the fault signal; everything else zeroed.
    status_ = ClimateStatus{};
}

RequestError ClimateManager::Cmd_ClimateSet(CommandContext& ctx)
{
    char  modeStr[8] = {};
    float sp = NAN;                  // absent stays NAN, so "no setpoint given" survives
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("mode",     modeStr),
        Optional("setpoint", sp)
    ));

    bool changed = false;
    {
        LOCK(mutex_);
        if (modeStr[0] != '\0')
        {
            ClimateMode m;
            if (ParseClimateMode(modeStr, m) && m != mode_) { mode_ = m; changed = true; }
        }
        if (!std::isnan(sp))
        {
            if (sp < kSetpointMin) sp = kSetpointMin;
            if (sp > kSetpointMax) sp = kSetpointMax;
            if (fabsf(userSetpoint_ - sp) > 0.001f) { userSetpoint_ = sp; changed = true; }
        }
        if (changed)
        {
            settingsDirty_ = true;
            lastChangeUs_ = esp_timer_get_time();   // persisted later by MaybeCommitSettings
        }
    }
    WriteStatus(ctx.out);
    return RequestError::Ok;
}

RequestError ClimateManager::Cmd_ClimateStatus(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());
    WriteStatus(ctx.out);
    return RequestError::Ok;
}

void ClimateManager::WriteStatus(Stream &out)
{
    ClimateMode mode;
    float userSp;
    ClimateStatus s;
    {
        LOCK(mutex_);
        mode = mode_;
        userSp = userSetpoint_;
        s = status_;
    }
    // The gateway reads "mode" from here over BLE, so it is the mode in force.
    mode = EffectiveMode(mode, serviceProvider_.getOpenThermManager().GetState());
    JsonObject resp(out);
    resp.field("mode", ClimateModeName(mode));
    resp.field("userSetpoint", userSp);
    resp.field("activeSetpoint", s.activeSetpoint);
    resp.field("roomTemp", s.roomTemp);
    resp.field("roomValid", s.roomValid);
    resp.field("pidOutput", s.pidOutput);
    resp.field("tSet", s.tSet);
    resp.field("chEnable", s.chEnable);
    resp.field("coolEnable", s.coolEnable);
    resp.field("overrideActive", s.overrideActive);
}
