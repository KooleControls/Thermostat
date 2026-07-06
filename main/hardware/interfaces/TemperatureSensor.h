#pragma once

// ──────────────────────────────────────────────────────────────
// Role interface: application vocabulary for "a temperature
// sensor" (ambient today; floor/outdoor later). Read returns
// false when no valid sample is available — that is the failure
// signal; there is no separate health probe.
//
// Calibration (placement/self-heating offset) is deliberately NOT
// here: it is user configuration owned by the application (see
// the room-temperature backlog item), not a driver property.
// ──────────────────────────────────────────────────────────────

class TemperatureSensor
{
public:
    virtual bool ReadTemperature(float &celsius) = 0;
    virtual ~TemperatureSensor() = default;
};
