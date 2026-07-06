#pragma once
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Role interface: transport for one OpenTherm frame exchange.
// Implemented by Stm32OpenThermLink (the STM32L051 co-processor
// owns the OT PHY). Consumers never see UARTs, resets or the
// STM32's heartbeat quirk — one call, one frame exchange.
// ──────────────────────────────────────────────────────────────
class OtLink
{
public:
    // Co-processor handshook and recently responsive.
    virtual bool Ready() const = 0;

    // One master->slave exchange: send a 32-bit OT frame, block for the
    // reply (sub-second). false = link/timeout error. An UNKNOWN-DATAID
    // reply is a SUCCESSFUL transaction — it arrives in `response`.
    virtual bool Transaction(uint32_t request, uint32_t &response) = 0;

    // Re-reset the STM32 into its app and redo the handshake (~1 s).
    virtual bool Recover() = 0;

    virtual ~OtLink() = default;
};
