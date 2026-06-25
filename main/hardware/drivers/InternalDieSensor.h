#pragma once

#include "driver/temperature_sensor.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// Reusable driver — ESP32-S3 internal die temperature sensor.
//
// Implements the board AmbientSensor HAL contract (see a board's
// AmbientSensor.h / ClimateManager): Init / ReadTemperature / ReadHumidity /
// HasHumidity / DefaultOffsetC / ok.
//
// It measures CHIP temperature (reads high due to self-heating), so
// DefaultOffsetC() applies a coarse correction toward room temperature. This
// is a fallback only — boards with no dedicated ambient sensor alias their
// AmbientSensor to this. No humidity.
// ──────────────────────────────────────────────────────────────

class InternalDieSensor
{
    static constexpr const char *TAG = "InternalTempSensor";

public:
    ~InternalDieSensor()
    {
        if (handle_)
        {
            temperature_sensor_disable(handle_);
            temperature_sensor_uninstall(handle_);
        }
    }

    bool Init()
    {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
        if (temperature_sensor_install(&cfg, &handle_) != ESP_OK)
        {
            ESP_LOGW(TAG, "install failed");
            handle_ = nullptr;
            return false;
        }
        if (temperature_sensor_enable(handle_) != ESP_OK)
        {
            ESP_LOGW(TAG, "enable failed");
            temperature_sensor_uninstall(handle_);
            handle_ = nullptr;
            return false;
        }
        ESP_LOGI(TAG, "Internal die temperature sensor ready");
        return true;
    }

    bool ReadTemperature(float &celsius)
    {
        if (!handle_) return false;
        return temperature_sensor_get_celsius(handle_, &celsius) == ESP_OK;
    }

    bool ReadHumidity(float &) { return false; }
    bool HasHumidity() const { return false; }
    // Nominal die self-heating correction toward room temperature.
    float DefaultOffsetC() const { return -23.0f; }
    bool ok() const { return handle_ != nullptr; }

private:
    temperature_sensor_handle_t handle_ = nullptr;
};
