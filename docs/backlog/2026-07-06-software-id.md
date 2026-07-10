# Software ID

**Infra I1.** Company-wide firmware identity.

- Reserved: **ID 28 (hex 0000001C)** on the canonical
  [Software ID's page](https://koolecontrolsdevelopment.atlassian.net/wiki/spaces/DEV/pages/430211074)
  (Development space) — group "KC Thermostat", ESP32-S3, TCP/IP ✓, BLE ✓.
- Embed in firmware at build time (CMake define, gateway-style) and report it
  in version/system info (web UI + commands).
- Used by the release workflow's artifact naming (see `release-workflow`).
