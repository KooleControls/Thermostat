#pragma once

#include "I2cBus.h"
#include "drivers/Aht20Sensor.h"

// DIYLESS Thermostat 3 — AHT20 ambient temp/humidity sensor on the shared I2C
// bus (SDA17/SCL18). Thin board adapter: supplies the board's I2C bus to the
// reusable Aht20Sensor driver and exposes the no-arg AmbientSensor HAL.
class AmbientSensor
{
public:
    bool Init() { return aht20_.Init(BoardI2cBus()); }
    bool ReadTemperature(float &celsius) { return aht20_.ReadTemperature(celsius); }
    bool ReadHumidity(float &percent) { return aht20_.ReadHumidity(percent); }
    bool HasHumidity() const { return aht20_.HasHumidity(); }
    float DefaultOffsetC() const { return aht20_.DefaultOffsetC(); }
    bool ok() const { return aht20_.ok(); }

private:
    Aht20Sensor aht20_;
};
