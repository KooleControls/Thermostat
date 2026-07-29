#pragma once

#include "driver/temperature_sensor.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// Reusable driver — the SoC's own on-die temperature sensor.
//
// This is DIE temperature, not room temperature: it sits tens of degrees above
// ambient and is only trustworthy for *relative* comparisons (a few degrees of
// absolute error is normal). It exists so we can tell how much of the room
// sensor's error is the board heating itself, and it is deliberately NOT a
// TemperatureSensor role implementation — nothing in the application may
// mistake it for an ambient source (see hardware/interfaces/).
//
// Board-independent: every ESP32-S3 has it, so it takes no pins and no bus.
// ──────────────────────────────────────────────────────────────

class SocTemperature
{
    static constexpr const char *TAG = "SocTemp";

public:
    bool Init()
    {
        // -10..80 °C is the calibration window closest to what a wall-mounted
        // unit's die actually runs at; a wider window costs accuracy.
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        esp_err_t err = temperature_sensor_install(&cfg, &sensor_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "install failed: %s", esp_err_to_name(err));
            sensor_ = nullptr;
            return false;
        }
        err = temperature_sensor_enable(sensor_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "enable failed: %s", esp_err_to_name(err));
            temperature_sensor_uninstall(sensor_);
            sensor_ = nullptr;
            return false;
        }
        return true;
    }

    /// false when the sensor is absent or the read failed.
    bool ReadCelsius(float &celsius)
    {
        if (!sensor_) return false;
        return temperature_sensor_get_celsius(sensor_, &celsius) == ESP_OK;
    }

    bool ok() const { return sensor_ != nullptr; }

private:
    temperature_sensor_handle_t sensor_ = nullptr;
};
