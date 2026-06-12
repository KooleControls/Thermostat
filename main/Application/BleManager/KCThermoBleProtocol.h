#pragma once
#include <stdint.h>
#include "host/ble_uuid.h"

// BLE GATT contract between the KC1245 gateway and the KC demo thermostat
// display.
//
// The GATEWAY is the peripheral/GATT server: it advertises this service with
// its gateway name (+ id) so a display can list nearby gateways and pick one.
// The DISPLAY is the central/client: it scans, connects to the selected
// gateway, subscribes to `control` and writes `measurement`/`intent`.
//
// !! An identical copy of this file lives in both projects — keep in sync !!
//   esp_gateway/main/Application/ThermostatManager/ThermostatDevices/KCThermoBleProtocol.h
//   Thermostat/main/Application/BleManager/KCThermoBleProtocol.h
//
// Characteristics (all multi-byte fields little-endian, packed):
//   measurement (write-no-rsp)   display -> gateway   KCThermoMeasurement
//   intent      (write-no-rsp)   display -> gateway   KCThermoIntent (seq bumps per user action)
//   control     (read|notify)    gateway -> display   KCThermoControl (authoritative state)
//
// Mode values on the wire: 0=Off 1=Heating 2=Cooling 3=Auto
// (matches the gateway's HvacMode and the display's ClimateMode enum order).
//
// The control characteristic has exactly one descriptor (its CCCD), so the
// central may assume CCCD handle == value handle + 1.

// 128-bit UUIDs; BLE_UUID128_INIT takes bytes least-significant first.
// Base: 6b2c4f5e-0d9a-42b1-8e47-3a1000109aXX
#define KC_THERMO_UUID128(last) BLE_UUID128_INIT( \
    (last), 0x9a, 0x10, 0x00, 0x10, 0x3a, 0x47, 0x8e, \
    0xb1, 0x42, 0x9a, 0x0d, 0x5e, 0x4f, 0x2c, 0x6b)

static const ble_uuid128_t KC_THERMO_SVC_UUID     = KC_THERMO_UUID128(0x01);
static const ble_uuid128_t KC_THERMO_MEAS_UUID    = KC_THERMO_UUID128(0x02);
static const ble_uuid128_t KC_THERMO_INTENT_UUID  = KC_THERMO_UUID128(0x03);
static const ble_uuid128_t KC_THERMO_CONTROL_UUID = KC_THERMO_UUID128(0x04);

enum : uint8_t
{
    KC_THERMO_INTENT_SETPOINT = 1,  // value = setpoint in 0.1 °C
    KC_THERMO_INTENT_MODE     = 2,  // value = mode (see above)
};

#pragma pack(push, 1)
struct KCThermoMeasurement
{
    int16_t roomTempTenths;
};

struct KCThermoIntent
{
    uint8_t seq;    // increments per user action; lets the gateway ignore a repeat
    uint8_t type;   // KC_THERMO_INTENT_*
    int16_t value;
};

struct KCThermoControl
{
    int16_t setpointTenths;
    uint8_t mode;
    uint8_t heating;   // legacy heat-output bool (kept for compatibility)
    uint8_t activity;  // KCThermoActivity — live HVAC state for the status icon
};
#pragma pack(pop)

// Values of KCThermoControl::activity (mirrors the gateway's ClimateActivity).
enum : uint8_t
{
    KC_ACTIVITY_OFF            = 0,  // off / idle           -> no icon
    KC_ACTIVITY_HEATING_IDLE   = 1,  // heating, not firing  -> grey flame
    KC_ACTIVITY_HEATING_ACTIVE = 2,  // actively heating     -> coloured flame
    KC_ACTIVITY_COOLING_IDLE   = 3,  // cooling, not firing  -> grey ice
    KC_ACTIVITY_COOLING_ACTIVE = 4,  // actively cooling     -> coloured ice
    KC_ACTIVITY_TRANS_TO_HEAT  = 5,  // switching -> heat    -> ice → flame
    KC_ACTIVITY_TRANS_TO_COOL  = 6,  // switching -> cool    -> flame → ice
};
