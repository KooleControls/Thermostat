# Command page (web UI)

**Status: idea, needs brainstorming — Bas wants a proper page, not a quick input box.** (2026-07-07)

A dedicated web-UI page for executing device commands — the interactive
surface for testing and diagnostics (change setpoint, toggle CH/DHW, read
OT status, …) without curl or bespoke UI per feature.

Deliberately **not** a one-line input field bolted onto the ConsolePage;
this deserves design time. Things a proper page could offer:

- Command discovery: list the registered commands (CommandManager knows
  the tables — needs an introspection command exposing name + owner).
- Per-command forms or a structured payload editor instead of raw JSON.
- Reply rendering (pretty JSON, errors distinguished from replies).
- History / repeat-last, favorites for bench workflows.

## Relations

- Depends on the command surface shape: see `2026-07-07-kc-command-tunnel.md` —
  whether commands stay JSON-typed, gain a KC-style (4-char ASCII)
  dialect, or both, changes what this page speaks.
- Overlaps `2026-07-06-web-integration.md` (item 8) — that item covers purpose-built
  OT status/control UI; this page is the generic escape hatch that keeps
  working for every future command without frontend changes.

## Open questions

- Command metadata: is name + free-form payload enough, or do commands
  declare a payload schema for form generation?
- Auth/safety: should destructive commands (reboot, factory reset) be
  gated or confirmed in the page?
- One page or part of ConsolePage (tab) — placement TBD in brainstorm.
