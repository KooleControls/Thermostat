# DIYLESS Thermostat 3 board target

**Step 1, item 1.** Add `main/hardware/boards/diyless_thermostat_3/` in the
Strux `Board`-class style, plus the shared drivers it needs:
`St7701Panel.h`, `Gt911Touch.h`, `Aht20Sensor.h` (and `Stm32OpenThermLink.h`,
consumed by the OT backlog items).

- Hardware: ESP32-S3, 2.1" round 480×480 ST7701 RGB LCD, GT911 touch (I2C),
  AHT20 temp/humidity, rotary knob, STM32L051 OT co-processor.
- Mine branch `feature/ot-thermostat-dropin` (board folder + drivers were
  proven there) but adapt to the current `hardware/interfaces` layering —
  don't copy the old board-folder shape verbatim.
- Board `sdkconfig.defaults`: esp32s3 target, octal PSRAM, 8 MB flash.
- Known quirk: don't use the ST7701 IO-mux path (RA2-398 notes).
- CI: release workflow builds `-DBOARD=diyless_thermostat_3`.

Done when: firmware builds and boots on the DIYLESS T3 (display stays dark —
no display manager yet; that's `thermostat-ui`).
