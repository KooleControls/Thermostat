#include "ClimateManager.h"
#include "BleManager/BleManager.h"
#include "SettingsManager/SettingsManager.h"
#include "esp_log.h"
#include <cmath>
#include <cstdlib>

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

    // The board's Sensor HAL supplies a nominal base offset (0 for a real
    // ambient sensor like the AHT20; a coarse self-heating correction for the
    // internal die-sensor fallback). The climate.tempOff NVS setting is added
    // on top for per-unit fine adjustment.
    int offsetTenths = serviceProvider_.getSettingsManager().getInt("climate.tempOff", 0);
    tempOffset_ = sensor_.DefaultOffsetC() + offsetTenths / 10.0f;

    if (!sensor_.Init())
        ESP_LOGW(TAG, "No temperature sensor available, holding 20.0°C");

    // Authoritative state pushed by the gateway (runs on the NimBLE host task).
    serviceProvider_.getBleManager().SetControlHandler([this](const KCThermoControl &control)
    {
        LOCK(mutex_);
        state_.setpoint = control.setpointTenths / 10.0f;
        if (control.mode <= (uint8_t)ClimateMode::Auto)
            state_.mode = (ClimateMode)control.mode;
        state_.heating = control.heating != 0;
        state_.activity = control.activity;
        gatewayConfirmed_ = true;
    });

    PollSensor();  // have a real reading before the UI first renders

    pollTimer_.Init("climate_poll", pdMS_TO_TICKS(PollPeriodMs), true);
    pollTimer_.SetHandler([this]() { PollSensor(); });
    pollTimer_.Start();

    init.SetReady();
    int totalTenths = (int)lroundf(tempOffset_ * 10.0f);
    ESP_LOGI(TAG, "Initialized (sensor: %s%s, total offset: %d.%d°C)",
             sensor_.ok() ? "present" : "none",
             sensor_.HasHumidity() ? "+humidity" : "",
             totalTenths / 10, abs(totalTenths) % 10);
}

ClimateState ClimateManager::GetState() const
{
    LOCK(mutex_);
    ClimateState copy = state_;
    copy.linked = gatewayConfirmed_ && serviceProvider_.getBleManager().IsConnected();
    return copy;
}

void ClimateManager::AdjustSetpoint(float delta)
{
    RETURN_IF_NOT_READY(initState_);
    int16_t tenths;
    {
        LOCK(mutex_);
        float value = state_.setpoint + delta;
        if (value < SetpointMin) value = SetpointMin;
        if (value > SetpointMax) value = SetpointMax;
        state_.setpoint = value;  // optimistic echo, gateway confirms/corrects
        tenths = (int16_t)lroundf(value * 10.0f);
    }
    serviceProvider_.getBleManager().SendIntent(KC_THERMO_INTENT_SETPOINT, tenths);
}

void ClimateManager::SetMode(ClimateMode mode)
{
    RETURN_IF_NOT_READY(initState_);
    {
        LOCK(mutex_);
        state_.mode = mode;  // optimistic echo, gateway confirms/corrects
    }
    serviceProvider_.getBleManager().SendIntent(KC_THERMO_INTENT_MODE, (int16_t)mode);
}

void ClimateManager::PollSensor()
{
    float celsius = 0.0f;
    if (!sensor_.ReadTemperature(celsius))
        return;
    celsius += tempOffset_;

    float humidity = -1.0f;
    bool haveHumidity = sensor_.HasHumidity() && sensor_.ReadHumidity(humidity);

    bool publish;
    {
        LOCK(mutex_);
        state_.roomTemp = celsius;
        if (haveHumidity)
            state_.roomHumidity = humidity;
        pollsSincePublish_++;
        publish = pollsSincePublish_ >= PublishEveryNthPoll ||
                  fabsf(celsius - lastPublishedTemp_) >= PublishDelta;
    }
    if (publish)
        PublishRoomTemp();
}

void ClimateManager::PublishRoomTemp()
{
    float celsius;
    {
        LOCK(mutex_);
        celsius = state_.roomTemp;
        lastPublishedTemp_ = celsius;
        pollsSincePublish_ = 0;
    }
    serviceProvider_.getBleManager().SendMeasurement((int16_t)lroundf(celsius * 10.0f));
}
