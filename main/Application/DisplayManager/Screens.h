#pragma once

class ServiceProvider;

// Each screen builds itself on a fresh LVGL screen object and loads it,
// deleting the previous screen (and its timers via LV_EVENT_DELETE).
// All Show* functions must be called from the LVGL task (or under
// lvgl_port_lock).

// Main thermostat view: temperature, setpoint, modes, gateway status,
// and the gear button into the PIN-protected configuration.
void ShowThermostatScreen(ServiceProvider &ctx);

// PIN entry guarding the configuration (setting "device.pin", "0000" when unset).
void ShowPinScreen(ServiceProvider &ctx);

// Gateway selection: scans for KC1245 gateways and connects to the tapped one.
void ShowGatewayScreen(ServiceProvider &ctx);
