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
        vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
    }
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
        { LOCK(mutex_); lastRoomValid_ = false; }
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
        activeSp = boiler.overrideSetpoint;
        overrideActive = true;
    }
    else if (mode == ClimateMode::Off)
    {
        activeSp = kFrostSetpointC;
    }
    else
    {
        activeSp = userSp;
    }

    if (overrideActive && !lastOverrideActive_)
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
    lastRoomTemp_ = room;
    lastRoomValid_ = true;
    lastActiveSetpoint_ = activeSp;
    lastPidOutput_ = output;
    lastTSet_ = tset;
    lastChEnable_ = ch;
    lastCoolEnable_ = cool;
    lastOverrideActive_ = overrideActive;
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
    lastChEnable_ = false;
    lastCoolEnable_ = false;
    lastTSet_ = 0.0f;
    lastPidOutput_ = 0.0f;
    lastOverrideActive_ = false;
}

void ClimateManager::Cmd_ClimateSet(Stream &in, Stream &out)
{
    JsonReader<128> json(in);
    ClimateMode mode;
    float setpoint;
    bool changed = false;
    {
        LOCK(mutex_);
        char modeStr[8] = {};
        if (json.GetString("mode", modeStr, sizeof(modeStr)))
        {
            ClimateMode m;
            if (ParseClimateMode(modeStr, m) && m != mode_) { mode_ = m; changed = true; }
        }
        float sp = json.GetFloat("setpoint", NAN);
        if (!std::isnan(sp))
        {
            if (sp < kSetpointMin) sp = kSetpointMin;
            if (sp > kSetpointMax) sp = kSetpointMax;
            if (fabsf(userSetpoint_ - sp) > 0.001f) { userSetpoint_ = sp; changed = true; }
        }
        mode = mode_;
        setpoint = userSetpoint_;
    }
    if (changed)
    {
        modeSetting_.Set((uint32_t)mode);
        setpointSetting_.Set(setpoint);
        serviceProvider_.getSettingsManager().Save();
    }
    WriteStatus(out);
}

void ClimateManager::Cmd_ClimateStatus(Stream &, Stream &out)
{
    WriteStatus(out);
}

void ClimateManager::WriteStatus(Stream &out)
{
    ClimateMode mode;
    float userSp, room, activeSp, output, tset;
    bool  roomValid, ch, cool, ovr;
    {
        LOCK(mutex_);
        mode = mode_;
        userSp = userSetpoint_;
        room = lastRoomTemp_;
        roomValid = lastRoomValid_;
        activeSp = lastActiveSetpoint_;
        output = lastPidOutput_;
        tset = lastTSet_;
        ch = lastChEnable_;
        cool = lastCoolEnable_;
        ovr = lastOverrideActive_;
    }
    JsonObject resp(out);
    resp.field("mode", ClimateModeName(mode));
    resp.field("userSetpoint", userSp);
    resp.field("activeSetpoint", activeSp);
    resp.field("roomTemp", room);
    resp.field("roomValid", roomValid);
    resp.field("pidOutput", output);
    resp.field("tSet", tset);
    resp.field("chEnable", ch);
    resp.field("coolEnable", cool);
    resp.field("overrideActive", ovr);
}
