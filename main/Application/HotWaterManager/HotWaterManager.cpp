#include "HotWaterManager.h"
#include "CommandManager/CommandManager.h"
#include "OpenThermManager.h"
#include "SettingsManager/SettingsManager.h"
#include "JsonScope.h"
#include "JsonReader.h"
#include "esp_log.h"
#include <cmath>

HotWaterManager::HotWaterManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void HotWaterManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);
    serviceProvider_.getSettingsManager().Register({ &enableSetting_, &setpointSetting_ });

    {
        LOCK(mutex_);
        enable_ = enableSetting_.Get();
        setpoint_ = setpointSetting_.Get();
    }
    Push();   // establish the DHW demand slice from persisted values

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (dhw %s, setpoint %.1f)",
             enable_ ? "on" : "off", setpoint_);
}

void HotWaterManager::Push()
{
    bool  en;
    float sp;
    { LOCK(mutex_); en = enable_; sp = setpoint_; }
    serviceProvider_.getOpenThermManager().SetDhwDemand(en, sp);
}

void HotWaterManager::Cmd_HotWaterSet(Stream &in, Stream &out)
{
    JsonReader<128> json(in);
    bool changed = false;
    {
        LOCK(mutex_);
        int i = json.GetInt("enable", -1);
        if (i >= 0)
        {
            bool v = (i != 0);
            if (enable_ != v) { enable_ = v; changed = true; }
        }
        float f = json.GetFloat("setpoint", NAN);
        if (!std::isnan(f))
        {
            if (f < kSetpointMin) f = kSetpointMin;
            if (f > kSetpointMax) f = kSetpointMax;
            if (fabsf(setpoint_ - f) > 0.001f) { setpoint_ = f; changed = true; }
        }
    }
    if (changed)
    {
        bool  en;
        float sp;
        { LOCK(mutex_); en = enable_; sp = setpoint_; }
        enableSetting_.Set(en);
        setpointSetting_.Set(sp);
        serviceProvider_.getSettingsManager().Save();
        Push();
    }
    WriteStatus(out);
}

void HotWaterManager::Cmd_HotWaterStatus(Stream &, Stream &out)
{
    WriteStatus(out);
}

void HotWaterManager::WriteStatus(Stream &out)
{
    bool  en;
    float sp;
    { LOCK(mutex_); en = enable_; sp = setpoint_; }
    OtBoilerState s = serviceProvider_.getOpenThermManager().GetState();

    JsonObject resp(out);
    resp.field("enable", en);
    resp.field("setpoint", sp);
    resp.field("dhwActive", s.dhwActive);
    resp.field("dhwTemp", s.dhwTemp);
    resp.field("dhwPresent", s.dhwPresent);
}
