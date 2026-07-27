# Use the ESPHome control loop

**Status: direction set, not urgent, not started.** (logged 2026-07-27) With
open-source approved, we may now use ESPHome's GPLv3 climate/PID control loop
directly (its actual source), instead of our own clean-room reimplementation.
This item is that rebuild. Needs its own brainstorm → spec → plan when picked up.

**Not blocking anything** — a working control loop already exists (item 5,
`climate-pid`, done clean-room: kp 0.77 / ki 0.0005 / kd 0, ±0.5 deadband,
10/15-sample averaging). This is a deliberate swap, to be done with evidence, not
rushed.

## Why this is now allowed

Previously ESPHome (GPLv3) forced a choice: ship their source and go GPL, or stay
closed and reimplement. We reimplemented clean-room. Now that the thermostat is
open-source (approved 2026-07-27, Jira RA2-395), shipping ESPHome-derived source
is fine. The repo currently ships under **MIT**; adopting ESPHome's source would
migrate the licence to **GPLv3** — this item owns that migration. See
`docs/superpowers/specs/2026-07-27-open-source-and-release-workflow-design.md`.

## Validate before swapping (important)

The current clean-room loop **demos correctly but is not validated across all
conditions** (deadband edges, cold start, odd boiler behaviour, sensor faults
mid-loop, override interplay). So the first move is *measurement*, not a rewrite:

- Exercise the existing loop against the **OTGW boiler-sim rig** to find where it
  actually falls short.
- Only then decide: harden ours, or adopt ESPHome's — and, if adopting, confirm
  ESPHome's does better in *our* setup rather than assuming it.

## Likely scope (if we proceed)

- Bring in ESPHome's control-loop source under GPLv3 with correct attribution,
  behind the existing `ClimateManager` boundaries so the rest of the firmware and
  the command/data surface are unaffected.
- Reconcile constants/behaviour with the DIYLESS reference; keep `t_set` clamping,
  zero-means-zero, frost-safe Off, and the override adoption we already have.
- Re-verify on hardware + boiler-sim.

## Relations

- Coupled to the licence decision (repo is MIT; adopting ESPHome → GPLv3
  migration) — see the open-source spec under `docs/superpowers/specs/`.
- Replaces the internals of item 5 (`climate-pid`); keep its external contract.
- Do this **last** of the current batch — after the two update UIs, and after the
  validation above.
