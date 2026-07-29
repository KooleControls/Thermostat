---
id: 2026-07-29-14h36-2
date: 2026-07-29
time: "14:36"
title: WiFi throughput traded away for internal DRAM
supersedes:
---

BLE does not fit on this board at ESP-IDF's default memory settings, and the fix
was a trade rather than a tweak, so it deserves recording. The BLE controller
claims ~45 KB of **internal** DRAM inside `nimble_port_init`, every FreeRTOS stack
must also be internal, and with the display and WiFi already up there was 3.8 KB
left — four bytes short of the 4 KB stack NimBLE's host task needs, which it then
failed to allocate *silently* because `esp_nimble_enable()` ignores the result of
`xTaskCreatePinnedToCore`. Two things bought the room back. **LVGL's draw buffer
went from 20 lines to 10** (480×20×2 = 19 KB of internal DMA RAM, halved); same
location and bounce-buffer mode, so no artifact risk, only smaller render batches.
**WiFi's default buffers were cut** — `STATIC_RX_BUFFER_NUM` from 10 to 4 (each is
~1.6 KB of internal DMA memory), the dynamic pools and BA window to match, and
`SPIRAM_MALLOC_ALWAYSINTERNAL` from 16 KB to 2 KB so medium allocations stop
landing in exactly the memory BLE wants. Result, measured: 48 → 83.7 KB free before
the stack, 3.2 → 17.9 KB after it. **The price is WiFi throughput under sustained
load** — fewer buffers means more retries when something streams hard, so a large
`.bin` upload through the web UI is slower than it was. Accepted because the web UI
is interactive and firmware now arrives over BLE; it would be the wrong trade on a
device whose WiFi carried real data. Rejected as worse, not merely unhelpful: moving
NimBLE's heap to PSRAM and moving the WiFi/LWIP buffers to PSRAM both made the
controller fail its *own* internal malloc and assert (`BLE assert emi.c 164`),
which boot-loops the panel — `BleManager` now refuses to start below a measured DRAM
floor so that failure can never reach a user. Consequence to keep in mind: internal
DRAM is now the scarce resource here, so the next feature that wants an internal
buffer or a task stack is competing with BLE, and the LVGL buffer is the 9.6 KB
lever if the argument has to be settled.

Decision: trade WiFi throughput and half the LVGL draw buffer for the internal DRAM
BLE needs; keep NimBLE's heap and the WiFi buffers in internal RAM.
