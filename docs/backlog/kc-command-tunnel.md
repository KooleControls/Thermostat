# KC-style commands + KC command tunnel over OpenTherm

**Status: idea, needs refinement — direction not decided yet.** (Bas, 2026-07-07)

Two related steps:

## 1. Make the thermostat's commands work like standard KC commands

Rework the command surface (currently WebSocket/HTTP JSON commands like
`otStatus`/`otSet`) to follow the standard KC command conventions used
across the product line (gateway KC1/KC2, doorlock, …): short ASCII
command prefixes with positional payloads, matching the existing KC
command table idioms (see the gateway's `main/source/` KC command table).

## 2. Tunnel KC commands to the thermostat through the gateway

Goal: reach the thermostat from the connection server **without the
thermostat being on the network** — the gateway relays over the OT wire.

Flow: client → connection server → gateway (KC command) → OT tunnel →
thermostat → reply back up the same path.

First sketch (needs refinement):

- Client sends the gateway a command with a relay prefix, e.g. **`CTUNxxyyzz`**
  - `xx` = software ID of the target (thermostat = 28/0x1C)
  - `yy` = device ID
  - `zz` = the command for the target
- The gateway recognizes `CTUN`, doesn't execute it, and forwards `zz`
  to the addressed device over the OT link (KC-frame mailbox / custom
  data-IDs — see RA2-395 Step 2 and RA2-396 for the OT transport question).
- The reply comes back as **`RTUNxxyypp`** where `pp` is the thermostat's
  response payload.

**Prior art to study:** how the doorlock does command relaying — same
pattern (addressed relay through an intermediary) already exists there.

## Open questions

- Is CTUN/RTUN the right shape at all? (Explicitly undecided.)
- Addressing: software ID + device ID enough? What about multiple
  thermostats on one gateway (OT is point-to-point, so maybe moot)?
- Transport: gated by RA2-396 (can the gateway's OT co-processor pass
  arbitrary data-IDs?) and the thermostat-side STM32 equivalent.
- Framing/segmentation: KC commands are longer than one 16-bit OT data
  value — needs the mailbox/sequencing scheme from the RA2-395 Step 2
  notes (opcode + payload word + ack).
- Relationship to the gateway-side THR-identity question (member-ID
  detection vs. dedicated `CSHWTHR` type).
