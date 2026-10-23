#pragma once

#include "ServiceProvider.h"
#include <cstddef>

// The display's access gate: knows which code opens the service menu and
// answers "is this code right?".
//
// The code is six digits and is never typed in anywhere: with a gateway paired
// it is that gateway's install code, which the installer already has; with none
// it is 000000. Deliberately *not* Authenticator (that is the web credential).
//
// A plain class, not a manager: DisplayManager holds it and hands it to the PIN
// screen, which keeps that screen free of any knowledge of the gateway link.
class PinGate
{
public:
    static constexpr size_t Len = 6;

    explicit PinGate(ServiceProvider& serviceProvider) : serviceProvider_(serviceProvider) {}

    bool Check(const char* entered) const;

    /// Whose code it is, for the installer standing at the panel: the paired
    /// gateway's name in `name` and its gateway id in `id`, either of which may
    /// come back empty, and its address in `name` when it gave neither (so
    /// `nameCap` is at least 18). False when no gateway is paired, and the code
    /// is 000000.
    bool Owner(char* name, size_t nameCap, char* id, size_t idCap) const;

    /// Whether the code is the paired gateway's install code. Not for a gateway
    /// paired before install codes were kept: that still opens with 000000.
    bool UsesInstallCode() const;

private:
    static constexpr const char* kUnpairedCode = "000000";

    ServiceProvider& serviceProvider_;
};
