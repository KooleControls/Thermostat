#include "TemperatureSensor.h"
#include "esp_log.h"

InternalTemperatureSensor::~InternalTemperatureSensor()
{
    if (handle_)
    {
        temperature_sensor_disable(handle_);
        temperature_sensor_uninstall(handle_);
    }
}

bool InternalTemperatureSensor::Init()
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&cfg, &handle_) != ESP_OK)
    {
        ESP_LOGW(TAG, "Install failed");
        handle_ = nullptr;
        return false;
    }
    if (temperature_sensor_enable(handle_) != ESP_OK)
    {
        ESP_LOGW(TAG, "Enable failed");
        temperature_sensor_uninstall(handle_);
        handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Internal die temperature sensor ready");
    return true;
}

bool InternalTemperatureSensor::Read(float &celsius)
{
    if (!handle_)
        return false;
    return temperature_sensor_get_celsius(handle_, &celsius) == ESP_OK;
}
