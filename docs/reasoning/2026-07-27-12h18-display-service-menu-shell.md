---
id: 2026-07-27-12h18
date: 2026-07-27
time: "12:18"
title: Display service-menu shell, gated by a PIN
supersedes:
---

Both WiFi backlog items (connect-to-AP and pull-a-firmware) say they need a
service menu on the display and that it should be built once, so we built that
shell first rather than growing either feature its own navigation. `DisplayManager`
became a shell that owns screens by value and hands them a narrow `Navigator`
interface, because letting screens call back into the manager would make the
include graph cyclic once the manager holds them as members. For menu content we
picked hand-built purpose-screens over generating rows from the settings registry
(the way the web `SettingsPage` does): the registry route is free and always
current, but a WiFi flow is a scan list, an on-screen keyboard and a progress
bar — none of which a generic setting row can express — and it would put raw keys
like `web.password` in front of an end user. Rows for features that don't exist
yet are added *disabled* rather than omitted or pointed at stubs, so the menu
reads as complete without any dead navigation. The gate is a new `ui.pin` setting
defaulting to `0000`, deliberately separate from `web.password`: that one is a
free-text web credential defaulting to empty (auth off), and a unit in a hallway
should be gated out of the box with something typeable on a keypad. Rejected
gating only the risky screens (more code, and no end user needs the menu at all)
and reusing `web.password` (wrong default, wrong input shape). The gate sits at
the single gear entry point, and any non-home screen falls back to home after 60 s
untouched so a unit is never left sitting unlocked in the menu.

Decision: build the shell now — `Navigator` + screens owned by `DisplayManager`,
hand-built purpose-screens, and a `ui.pin` gate (default `0000`) on the gear.
