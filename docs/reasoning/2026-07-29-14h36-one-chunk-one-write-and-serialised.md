---
id: 2026-07-29-14h36
date: 2026-07-29
time: "14:36"
title: One session chunk is one GATT write, and writes are serialised
supersedes:
---

Two transport decisions that were made while building and had only lived in commit
messages. **A session chunk is exactly one GATT operation**: the chunk payload is
sized from the negotiated MTU (247 → 241 bytes after the ATT and session headers),
which means **no fragmentation or reassembly layer exists on either side** — the
same property the WebSocket has, where one chunk is one WS frame. The alternative,
a chunk size independent of the MTU with a fragmentation layer underneath, buys
nothing here and would have to be written and debugged twice, once per end.
**Writes are then serialised**, each waiting for its GATT completion callback
before the next starts. That was not the original design — firing them back to
back is obviously faster and it worked fine for every single-chunk reply. The
first multi-chunk reply failed immediately with `rc 6` (`BLE_HS_ENOMEM`): nothing
freed NimBLE's mbufs between writes, so the pool ran dry mid-reply. Waiting per
write fixes that and doubles as flow control — one write in flight means the peer
never has to buffer more than a chunk, which is what makes a future firmware push
bounded rather than hopeful. Rejected the two cheaper repairs: retrying blindly on
ENOMEM (spins against a drained pool and still has no backpressure) and enlarging
the mbuf pool (raises the ceiling without bounding anything). Cost accepted: a
round trip per chunk, so `getSettings` came back in ~1 s — though most of that is
the thermostat reading settings out of NVS, not the radio, and the per-chunk cost
was never isolated. Worth measuring properly before anyone sizes a firmware
transfer from it, and a shorter connection interval is the obvious lever if it
turns out to matter.

Decision: chunk size follows the MTU so there is no fragmentation layer, and each
outbound write waits for its completion before the next is issued.
