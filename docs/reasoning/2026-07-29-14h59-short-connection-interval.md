---
id: 2026-07-29-14h59
date: 2026-07-29
time: "14:59"
title: A short connection interval, because latency is the interval
supersedes: 2026-07-29-14h36
---

Refining the "cost accepted" line in 14h36 with numbers, because the lever it
deferred turned out to be the whole story. A command over BLE costs a notify one
way plus a write the other, so it takes a few **connection events** no matter how
efficient the code is — at NimBLE's default 30–50 ms interval a warm `ping` round
trip measured **193 ms**, which reads like a slow link and is really just four
intervals. As the central we set the interval at connect time, so it asks for
**15–20 ms** (`itvl_min` 12, `itvl_max` 16) with zero slave latency and a 4 s
supervision timeout. Measured after: warm `ping` **33–74 ms**, a ~1 KB multi-chunk
`getSettings` **995 → 335 ms**, and the first command after a reconnect **1737 →
548 ms** — that last one because discovery is several round trips too, so it was
interval-bound as well. The usual argument for a long interval is battery life and
it does not apply here: both boxes are mains-powered. What we are spending instead
is **radio airtime on the gateway, where BLE shares one antenna path with WiFi** —
which is why this stops at 15 ms rather than the 7.5 ms minimum, and it is the
first thing to widen again if WiFi throughput or BLE stability degrades on that
side. Everything else in 14h36 stands: chunk size follows the MTU, and each write
waits for its completion. Note the per-chunk radio cost is still not isolated from
the work the thermostat does per command (`getSettings` re-reads NVS), so size a
firmware transfer from a measurement of *that*, not from these figures.

Decision: request a 15–20 ms connection interval from the central at connect time;
revisit only if the gateway's WiFi/BLE coexistence complains.
