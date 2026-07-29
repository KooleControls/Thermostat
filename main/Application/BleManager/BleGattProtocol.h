#pragma once

#include "host/ble_uuid.h"
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// The BLE contract between this thermostat and a KC1245 Gateway.
//
// Role split: the **gateway advertises**, so the gateway is the peripheral and
// hosts the GATT table; we are the central and discover it as a client. That
// puts the characteristic definitions on the gateway even though the command
// language they carry is ours — see
// docs/reasoning/2026-07-29-11h03-what-goes-in-the-advertisement.md.
//
// This file is deliberately KC-specific. It is allowed here because the whole
// BLE module is rip-out-able: delete it and the thermostat still works over
// WiFi or any transport someone else writes —
// docs/reasoning/2026-07-29-10h55-kc-specific-code-lives-in-the-ble-manager.md.
// ──────────────────────────────────────────────────────────────
namespace kcble
{
    // 4b4300xx-9c1d-4f2e-be3a-6d5c1f0a7e10 — 0x4B43 is "KC", the tail is random.
    // BLE_UUID128_INIT takes the bytes little-endian, i.e. the string reversed.

    // The command-transport service.
    inline constexpr ble_uuid128_t SERVICE_UUID = BLE_UUID128_INIT(
        0x10, 0x7e, 0x0a, 0x1f, 0x5c, 0x6d, 0x3a, 0xbe,
        0x2e, 0x4f, 0x1d, 0x9c, 0x01, 0x00, 0x43, 0x4b);

    // Gateway → thermostat. The gateway notifies session chunks on this one; we
    // subscribe to it. Requests arrive here.
    inline constexpr ble_uuid128_t CHR_INBOUND_UUID = BLE_UUID128_INIT(
        0x10, 0x7e, 0x0a, 0x1f, 0x5c, 0x6d, 0x3a, 0xbe,
        0x2e, 0x4f, 0x1d, 0x9c, 0x02, 0x00, 0x43, 0x4b);

    // Thermostat → gateway. We write session chunks here. Replies leave here.
    inline constexpr ble_uuid128_t CHR_OUTBOUND_UUID = BLE_UUID128_INIT(
        0x10, 0x7e, 0x0a, 0x1f, 0x5c, 0x6d, 0x3a, 0xbe,
        0x2e, 0x4f, 0x1d, 0x9c, 0x03, 0x00, 0x43, 0x4b);

    // ── Advertising payload ────────────────────────────────────
    // The gateway id rides as manufacturer data in the advertising packet; the
    // gateway *name* rides in the scan response, because 31 bytes per packet
    // will not hold both. The install code is never advertised — as a pairing
    // passkey it is never transmitted at all.

    // Bluetooth-SIG test id. KC holds no assigned company id; if one is ever
    // registered, both sides change together.
    inline constexpr uint16_t COMPANY_ID = 0xFFFF;

    // First payload byte after the company id — says "this is a KC gateway"
    // and versions the layout, so a future field can be added without
    // confusing an older thermostat.
    inline constexpr uint8_t MFG_TYPE_GATEWAY_V1 = 0x01;

    // [ company:u16 LE ][ type:u8 ][ dgid:u32 LE ]
    inline constexpr size_t MFG_DATA_LEN = 2 + 1 + 4;
    inline constexpr size_t MFG_OFF_TYPE = 2;
    inline constexpr size_t MFG_OFF_DGID = 3;
}
