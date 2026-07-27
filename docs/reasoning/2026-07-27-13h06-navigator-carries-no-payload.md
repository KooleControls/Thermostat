---
id: 2026-07-27-13h06
date: 2026-07-27
time: "13:06"
title: Navigator carries no payload, so the WiFi flow is one screen
supersedes:
---

The WiFi connect flow is one screen with two internal views (network list ↔
passphrase pad) rather than two screens, because a separate password screen would
need the chosen SSID handed to it and `Navigator` deliberately can't carry one — it
takes a bare `ScreenId`. Widening `Navigator` to pass arguments was the obvious
alternative and was rejected: it exists precisely so screens don't know about each
other (the shell owns them all by value, so a screen reaching another would make
the include graph cyclic), and one flow's convenience is not worth loosening that.
The cost is that a multi-step flow lives inside a single `Screen` subclass and gets
longer than a plain page; the benefit is that adding a screen stays a
four-line change and no screen ever depends on another. If a second multi-step flow
appears and this shape starts to hurt, the fix is a payload mechanism on the shell
(a "pending target" the destination screen reads in `OnShow`), not screens calling
each other.

Decision: keep `Navigator` payload-free; multi-step flows are internal views of one
screen.
