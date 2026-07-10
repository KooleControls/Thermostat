# Thermostat UI — visual design / polish

**Status: idea, deferred — Bas will finetune the look himself later.** (2026-07-07)

The minimal first-light screen (item 7) **works** — big room-temp number,
− / + setpoint buttons — but it **doesn't look good**. This item is the
visual pass to make it a proper, attractive thermostat face.

Not a functional gap; the plumbing (LVGL, panel, touch, `DisplayManager`,
`ClimateManager` control surface) is all in place. This is layout, styling,
and finish on top of what's there.

## Likely scope (Bas to refine)

- Overall layout/composition on the 480×480 round-ish panel — proportions,
  spacing, centering, button size/placement/shape.
- Typography, colours, background, contrast; a coherent visual style rather
  than plain white-on-black.
- Button feedback (press states), the temp→setpoint transition, maybe an
  animation on the revert.
- Fits with the broader fuller-UI item (mode, DHW, activity/flame, fault,
  OT-link indicator, setpoint arc) — decide whether polish lands first or
  rides along with adding those elements.

## Relations

- Builds on item 7 (`thermostat-ui`, minimal — done).
- Consider alongside `2026-07-07-ui-command-driven.md` (if the screen goes
  command-driven, a web replica shares the same layout/LVGL, so polish could
  be done once for both).
- The fuller home-screen elements (mode/DHW/icons/fault/arc) deferred from
  item 7 belong with this visual work.

## Note

Bas intends to do the finetuning himself — treat this as his canvas; the job
here is to keep the control/data surface clean and stable so restyling is
pure presentation.
