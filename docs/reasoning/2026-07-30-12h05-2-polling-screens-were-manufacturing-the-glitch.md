---
id: 2026-07-30-12h05-2
date: 2026-07-30
time: "12:05"
title: Polling screens were manufacturing the glitch
supersedes:
---

The idle half of the display glitch was self-inflicted, which is worth recording
as a convention rather than a bug fix. `lv_label_set_text()` invalidates the
label whether or not the string changed, and every screen polls its managers on a
timer and mostly writes back what is already there — so `HomeScreen` at 1 Hz and
`WifiScreen`/`BleScreen` at 2 Hz were each turning "nothing happened" into a real
redraw. Measured with LVGL's sysmon: sitting on the home screen doing nothing cost
a **25–35 ms software render plus a PSRAM flush burst every second, drawing
identical pixels** (0 redraws in 200 samples afterwards, versus roughly one per
second before; idle CPU 0–8 % → 0–2 %). That burst was not merely wasteful, it was
*the* visible artifact: the flush contends with the panel DMA for PSRAM bandwidth,
so the once-a-second "refresh" the user reported and the once-a-second vertical
jump were the same event. The fix is `Screen::SetLabelText()`, which compares
against `lv_label_get_text()` first — LVGL will hand back the current string, so
no caching is needed. **New screens must use it on every periodic path**;
`Build()` may call `lv_label_set_text` directly because it runs once. Rejected:
per-screen cached copies of each string (more state, same effect), and lowering
the poll rates (hides the cost instead of removing it, and the screens genuinely
want fresh data — it is the *writing* that was wrong, not the reading). Note that
this was found only because the glitch was measured rather than reasoned about;
two earlier fixes aimed at the DMA were guesses, and one of them made things
worse.

Decision: guard every periodic label write through `Screen::SetLabelText`.
