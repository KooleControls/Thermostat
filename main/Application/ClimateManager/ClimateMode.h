#pragma once
#include <cstring>

// What the guest picked on the panel. It decides which requests go out over
// OpenTherm (see ClimateManager::ControlStep), and the gateway also reads it;
// the gateway's state machine still owns the changeover and its timings. Auto
// leaves the choice of direction with the gateway, as it always was.
enum class ClimateMode { Off = 0, Heat = 1, Cool = 2, Auto = 3 };

inline const char *ClimateModeName(ClimateMode m)
{
    switch (m)
    {
    case ClimateMode::Heat: return "heat";
    case ClimateMode::Cool: return "cool";
    case ClimateMode::Auto: return "auto";
    default:                return "off";
    }
}

// Accepts "off"/"heat"/"cool" or "0"/"1"/"2". Returns false if unrecognized
// (caller keeps the current mode).
inline bool ParseClimateMode(const char *s, ClimateMode &out)
{
    if (!s) return false;
    if (!strcmp(s, "off")  || !strcmp(s, "0")) { out = ClimateMode::Off;  return true; }
    if (!strcmp(s, "heat") || !strcmp(s, "1")) { out = ClimateMode::Heat; return true; }
    if (!strcmp(s, "cool") || !strcmp(s, "2")) { out = ClimateMode::Cool; return true; }
    if (!strcmp(s, "auto") || !strcmp(s, "3")) { out = ClimateMode::Auto; return true; }
    return false;
}
