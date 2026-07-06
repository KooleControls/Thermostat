# Release workflow (gateway-style)

**Infra I2.** Rework `.github/workflows/release.yml` (currently stock Strux)
to match the gateway's release conventions:

- Tags `VX.Y.Z`; parse major/minor/patch, pass as
  `-DSOFTWARE_VERSION_MAJOR/MINOR/PATCH`; patch ≠ 0 ⇒ prerelease.
- Artifact naming `MM_mm_pp_<softwareId>_KC_<name>` (software ID 28).
- Artifacts: factory merged bin, app-only bin, www bin (keep Strux's set).
- Build for `-DBOARD=diyless_thermostat_3`, target esp32s3 (once the board
  target exists).
- Open question (parked): does the thermostat also need `.hex` /
  `.kczip` service-tool artifacts like the gateway (esphexbuilder), or are
  those gateway-only?
