---
id: 2026-07-27-13h06-2
date: 2026-07-27
time: "13:06"
title: WiFi credentials apply at runtime, not at boot
supersedes:
---

`NetworkManager::ConnectToStation()` persists new STA credentials *and* reconnects
immediately, because credentials were previously only read during `Init()` —
changing networks meant a reboot, which is unusable for a technician standing at
the unit. Rejected the reboot-to-apply path even though it was near-zero code (the
settings already persist and boot already reads them): it drops the web session and
looks like a crash to whoever is holding the thermostat. Also rejected exposing it
as a WebSocket command — the display is the caller, and the command surface is for
remote clients. Known trap: it tears down the fallback AP, which is right when the
caller is at the touchscreen but would strand anyone driving the same call from the
AP's own web UI, so a web-side WiFi page must not simply reuse it. The 3-attempt
then fall-back-to-AP behaviour is untouched, which is also why a wrong passphrase
surfaces as "own access point" rather than "wrong password".

Decision: add a runtime credential-apply call to NetworkManager; no reboot,
AP-fallback logic unchanged.
