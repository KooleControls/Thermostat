# Software ID

**Infra I1. Partially done — one piece still open.** Company-wide firmware identity.

- ✅ **Reserved: ID 28 (hex 0000001C)** on the canonical
  [Software ID's page](https://koolecontrolsdevelopment.atlassian.net/wiki/spaces/DEV/pages/430211074)
  (Development space) — group "KC Thermostat", ESP32-S3, TCP/IP ✓, BLE ✓.
- ✅ Used by the release workflow's artifact naming (the ID appears in the
  release filename — done with the release-workflow item).
- ⏳ **Open: embed the ID in the firmware itself** (CMake define, gateway-style)
  and report it in version/system info (web UI + commands). Deferred pending the
  open-source decision (until then the ID lives only in the release filename,
  not baked into the image).
