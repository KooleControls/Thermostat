#pragma once

#include "SettingsManager/SettingsManager.h"
#include "TypedSettings.h"
#include <cstring>

// The display's access gate: owns the PIN and answers "is this code right?".
//
// Deliberately *not* Authenticator (that is the web credential): this is a short
// numeric code typed on a touch keypad, and its default is non-empty so a unit
// is gated out of the box. Empty PIN means the gate is off — same convention as
// web.password, where empty disables auth.
//
// A plain class, not a manager: DisplayManager holds it and hands it to the PIN
// screen, which keeps that screen free of any settings knowledge.
class PinGate
{
public:
    static constexpr size_t MaxLen = 8;

    void Register(SettingsManager& settings) { settings.Register({ &pin_ }); }

    /// False when no PIN is stored — the settings menu then opens directly.
    bool Required() const
    {
        char stored[MaxLen + 1] = {};
        pin_.Get(stored, sizeof(stored));
        return stored[0] != '\0';
    }

    bool Check(const char* entered) const
    {
        if (entered == nullptr) return false;
        char stored[MaxLen + 1] = {};
        pin_.Get(stored, sizeof(stored));
        return std::strcmp(entered, stored) == 0;
    }

private:
    // NVS keys are limited to 15 characters.
    inline static StringSetting pin_{ "ui.pin", "Display PIN", "0000" };
};
