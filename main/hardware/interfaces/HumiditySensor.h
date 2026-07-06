#pragma once

// ──────────────────────────────────────────────────────────────
// Role interface: application vocabulary for "a humidity sensor".
// A board without one simply doesn't expose a humidity accessor —
// capability is the Board's compile-time surface, not a runtime
// probe. Read returns false when no valid sample is available.
// ──────────────────────────────────────────────────────────────

class HumiditySensor
{
public:
    virtual bool ReadHumidity(float &percent) = 0;
    virtual ~HumiditySensor() = default;
};
