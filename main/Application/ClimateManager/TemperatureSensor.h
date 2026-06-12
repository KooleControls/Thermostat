#pragma once

#include "driver/temperature_sensor.h"

// Room-temperature source abstraction. The demo uses the ESP32-S3 internal
// die sensor (or a fixed value if that fails); a real room sensor can be
// dropped in later without touching the climate logic.
class ITemperatureSensor
{
public:
    virtual ~ITemperatureSensor() = default;
    virtual bool Init() = 0;
    virtual bool Read(float &celsius) = 0;
};

// ESP32-S3 internal die temperature sensor. It measures chip temperature,
// not room temperature (reads high due to self-heating) — demo only.
// A calibration offset is applied by the ClimateManager.
class InternalTemperatureSensor : public ITemperatureSensor
{
    static constexpr const char *TAG = "InternalTempSensor";

public:
    ~InternalTemperatureSensor() override;
    bool Init() override;
    bool Read(float &celsius) override;

private:
    temperature_sensor_handle_t handle_ = nullptr;
};

// Fixed fallback when no sensor hardware is available.
class FixedTemperatureSensor : public ITemperatureSensor
{
public:
    explicit FixedTemperatureSensor(float celsius) : celsius_(celsius) {}
    bool Init() override { return true; }
    bool Read(float &celsius) override { celsius = celsius_; return true; }

private:
    float celsius_;
};
