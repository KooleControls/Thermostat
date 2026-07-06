# OpenTherm co-processor link driver

**Step 1, item 2.** `Stm32OpenThermLink` driver: the ESP32-S3 does not
bit-bang OT — an STM32L051 owns the OT PHY, driven over a serial nibble
protocol (200 kbaud) on GPIO12 (TX) / GPIO11 (RX), boot on GPIO44, reset on
GPIO13, ~900 ms warm-up after reset.

- Proven end-to-end on `feature/ot-thermostat-dropin` (RA2-398 was the
  de-risk) — port the driver, don't reinvent the protocol.
- Protocol reference: DIYLESS `esphome-opentherm-t3` external component.
- Lives in `hardware/drivers/`, pins from the board's `BoardConfig`.

Done when: a raw OT frame round-trips (e.g. read slave status/config) on the
DIYLESS T3.
