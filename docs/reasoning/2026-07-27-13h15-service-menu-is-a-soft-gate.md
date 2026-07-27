---
id: 2026-07-27-13h15
date: 2026-07-27
time: "13:15"
title: The service menu is a soft gate, not a security boundary
supersedes:
---

The service menu's PIN is a deterrent, not a security boundary: it exists to stop a
guest idly changing settings, so wrong entries retry immediately and indefinitely,
with no attempt counter, lockout, or delay. Rejected hardening it (rate limiting,
lockout after N tries) and rejected plumbing `WIFI_EVENT_STA_DISCONNECTED`'s reason
code through so a mistyped WiFi passphrase could report "wrong password" instead of
silently falling back to the unit's own AP — both buy robustness this surface
doesn't need, and entering a passphrase on a touch keyboard is already slow enough
that added friction costs more than the failure mode does. This sets the bar for the
rest of the menu, including the pending firmware screen: keep error handling simple
and let people retry. Note it also means the PIN is not protection against anyone
with time and intent, so nothing genuinely sensitive should sit behind it alone.

Decision: the service menu is a soft gate — unlimited silent retries, no hardening.
