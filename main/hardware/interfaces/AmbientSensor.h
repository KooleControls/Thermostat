#pragma once

// ──────────────────────────────────────────────────────────────
// Role interface: application vocabulary for "the ambient sensor"
// (room temperature/humidity). Boards bind a real driver
// (Aht20Sensor) — a board without one would bind a mock.
//
// Readings are raw sensor values: placement/self-heating
// calibration (offset) is the application's job (see the
// room-temperature backlog item); DefaultOffsetC() is the
// driver's suggested starting point for that offset.
// ──────────────────────────────────────────────────────────────

class AmbientSensor
{
public:
    virtual bool ReadTemperature(float &celsius) = 0;
    virtual bool ReadHumidity(float &percent) = 0;
    virtual bool HasHumidity() const = 0;
    virtual float DefaultOffsetC() const = 0;
    virtual bool ok() const = 0;
    virtual ~AmbientSensor() = default;
};
