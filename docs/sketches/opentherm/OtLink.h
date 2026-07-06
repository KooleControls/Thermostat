#pragma once
#include <cstdint>

// ══════════════════════════════════════════════════════════════
// DESIGN EXAMPLE (not compiled) — proposed hardware/interfaces/OtLink.h
// ══════════════════════════════════════════════════════════════
//
// Role interface: transport for one OpenTherm frame exchange.
//   • Real driver: Stm32OpenThermLink — the STM32L051 co-processor owns
//     the OT PHY; the ESP32 drives it over UART (nibble protocol, ported
//     verbatim from the proven demo branch, plus ": public OtLink").
//   • Boards without OT hardware bind MockOtLink (Ready() == false,
//     everything fails) — so OpenThermManager is plain application code
//     that compiles and runs on every board, no #ifdef BOARD_... like
//     the demo had.
//   • The diyless Board owns the driver instance and performs the STM32
//     reset + CpuStatus handshake in Board::Init() (bounded, ~1-2 s).

class OtLink
{
public:
    // Co-processor handshook and responsive.
    virtual bool Ready() const = 0;

    // One master->slave exchange: send a 32-bit OT frame, block for the
    // reply (sub-second). false = timeout/link error (NOT unknown-DataID —
    // that comes back as a valid UNKNOWN-DATAID frame in `response`).
    virtual bool Transaction(uint32_t request, uint32_t &response) = 0;

    // Re-reset the STM32 and redo the handshake (link supervision calls
    // this with backoff after repeated Transaction failures).
    virtual bool Recover() = 0;

    virtual ~OtLink() = default;
};
