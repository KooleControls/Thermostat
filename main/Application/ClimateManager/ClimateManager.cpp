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

    bool  overrideActive = false;
    float activeSp;
    if (mode != ClimateMode::Off && boiler.overrideSetpoint > 0.0f)
    {
        // Adopt the gateway's remote override as our own setpoint (RAM only —
        // deliberately NOT persisted): the display follows it and we stop
        // re-requesting the pre-override value, so the gateway sees its cap
        // satisfied and stops re-capping — otherwise ID 16 oscillates. The
        // user can still nudge locally afterwards; on reboot we boot to the
        // stored value and the gateway re-caps, so it self-heals.
        float ov = boiler.overrideSetpoint;
        if (ov < kSetpointMin) ov = kSetpointMin;
        if (ov > kSetpointMax) ov = kSetpointMax;
        activeSp = ov;
        overrideActive = true;
        { LOCK(mutex_); userSetpoint_ = ov; userSp = ov; }
    }
    else if (mode == ClimateMode::Off)
    {
        activeSp = kFrostSetpointC;
    }
    else
    {
        activeSp = userSp;
    }

    if (overrideActive && !status_.overrideActive)
        ESP_LOGI(TAG, "Remote override adopted: setpoint %.1f", activeSp);

    bool  ch = false, cool = false;
    float tset = 0.0f, output = 0.0f;

    if (mode == ClimateMode::Cool)
    {
        cool = boiler.coolingSupported && (room > activeSp + 0.5f);
        pid_.Reset();   // PID unused for cooling; keep it clean for next Heat
    }
    else   // Heat, or Off (frost) — both run the heating PID
    {
        output = pid_.Update(activeSp, room, dt);
        tset = OutputToTSet(output, boiler.maxTSetLower, boiler.maxTSetUpper);
        ch = (mode == ClimateMode::Heat) ? true : (tset > 0.0f);
    }

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
