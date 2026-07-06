# Thermostat on-screen UI

**Step 1, item 7.** There is no display stack on the Strux baseline — this
item introduces the display manager + LVGL and the thermostat home screen.

- LVGL 9 on the T3's round 480×480 ST7701 panel + GT911 touch (+ knob).
- Home screen: room temp, setpoint arc/±, mode (Heat/Cool/Off), DHW on/off +
  setpoint, activity icons (flame/heating/cooling/DHW), fault indication,
  OT link state.
- Backlight control + screen timeout (setting).
- The old demo's `DisplayManager`/screens on `feature/ot-thermostat-dropin`
  are reference only — the DIYLESS layout there was for a different set of
  screens; design fresh against local ClimateManager/OpenThermManager state.

Done when: everything controllable from the screen without web UI.
