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

RequestError HotWaterManager::Cmd_HotWaterSet(CommandContext& ctx)
{
    // Both optional, and both need "absent" to differ from a value. A bool cannot
    // carry that, so enable arrives as the same tri-state number the JSON reader
    // used, and the setpoint keeps NAN for "not given".
    uint32_t enable = 2;             // 0/1 set it; anything else leaves it alone
    float    f = NAN;
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("enable",   enable),
        Optional("setpoint", f)
    ));

    bool changed = false;
    {
        LOCK(mutex_);
        if (enable <= 1)
        {
            bool v = (enable != 0);
            if (enable_ != v) { enable_ = v; changed = true; }
        }
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
    WriteStatus(ctx.out);
    return RequestError::Ok;
}

RequestError HotWaterManager::Cmd_HotWaterStatus(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());
    WriteStatus(ctx.out);
    return RequestError::Ok;
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
