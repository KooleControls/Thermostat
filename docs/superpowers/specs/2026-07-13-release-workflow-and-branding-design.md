# Release workflow + branding — design

Covers two infra-track backlog items together (they overlap on naming):

- **I2 `release-workflow`** — rework `.github/workflows/release.yml` gateway-style
  (`docs/backlog/2026-07-06-release-workflow.md`).
- **I3 `branding`** — replace Strux identity with KC Thermostat identity
  (`docs/backlog/2026-07-06-branding.md`).

**I1 `software-id` stays deferred.** Open-source status for this repo is
undecided, and I1 (reserve ID 28 on the wiki + **embed the SID in firmware** +
report it in version/system info) would publish KC's internal software-identity
scheme. This design therefore does **not** add a `SOFTWARE_ID` compile define or
report the SID anywhere in firmware. It *does* use the real SID **28** in the
release-artifact **filename** — a build-artifact name is not a firmware
commitment. When I1 is picked up, embedding the SID in firmware is the remaining
work; the release name needs no change.

## Naming conventions (decided)

- **KC numbers are hardware.** The thermostat's hardware product number is
  **KC1247**; it does not appear in any software/firmware name. (The gateway's
  legacy `KC1245-gateway` is the old convention, not the pattern to copy.)
- **Firmware / CMake project / binary name:** `KCThermostat` (brand prefix "KC"
  is fine; a hardware *number* is not). This is the `<name>` slot in the release
  artifact.
- **Product display name:** `KC Thermostat` (with a space) — friendly, shown in
  UI and to the user. It is *not* hostname-safe, so surfaces that need a
  hostname derive a sanitized form (see branding below).
- **Version tags:** uppercase `VX.Y.Z` (Koole/gateway convention).

## I2 — Release workflow

### `.github/workflows/release.yml`

Rewrite the stock-Strux workflow to mirror the gateway's, keeping our frontend
build (the gateway has no frontend). Structure:

- **Trigger:** `push` on tags `V*`, plus `workflow_dispatch`.
- **Env constants block** (easy-to-change, gateway-style):
  - `PROJECT_NAME: "KCThermostat"`
  - `IDF_VERSION: "v6.0"`
  - `IDF_TARGET: "esp32s3"`
  - `BOARD: "diyless_thermostat_3"`
  - `SOFTWARE_ID: "28"`
  - `ARTIFACT_PREFIX: "KC"`
- **Parse version from tag** (`bash`, gateway's regex adapted): require
  `^V([0-9]+)\.([0-9]+)\.([0-9]+)$`, fail otherwise. Emit `major`/`minor`/`patch`
  outputs. `patch != 0 ⇒ prerelease=true`. Build the artifact base string
  `MM_mm_pp_<SOFTWARE_ID>_<ARTIFACT_PREFIX>_<PROJECT_NAME>`
  (e.g. `01_00_00_28_KC_KCThermostat`), zero-padded two-digit fields.
- **Frontend build:** pnpm setup + `cd frontend && pnpm install --frozen-lockfile
  && pnpm build` (as today). Runs before the firmware build so `www/` is
  populated for the FAT image.
- **Firmware build** via `espressif/esp-idf-ci-action@v1`:
  `idf.py set-target esp32s3` then
  `idf.py -DBOARD=diyless_thermostat_3 -DSOFTWARE_VERSION_MAJOR=… -DSOFTWARE_VERSION_MINOR=… -DSOFTWARE_VERSION_PATCH=… build`,
  then `esptool merge_bin @flash_args -o merged-factory.bin`.
- **Artifacts — bins only** (Strux's set, renamed to the artifact base):
  - `<base>-factory.bin` — merged factory image (bootloader + parts + app + www)
  - `<base>.bin` — application-only (web-UI OTA)
  - `<base>-www.bin` — FAT web image (independent OTA)
  No `.hex` / `.kczip`: those are gateway service-tool formats; the thermostat
  updates over the web UI. Revisit only if KC service tooling needs to flash it.
- **Create GitHub Release** (`softprops/action-gh-release@v2`): pass
  `prerelease: ${{ steps.version.outputs.prerelease }}`,
  `generate_release_notes: true`, a `body:` describing the three files, and the
  three artifact paths under `files:`.

### `CMakeLists.txt` (root)

Today it derives `PROJECT_VER` only from a lowercase `v` git tag and ignores
injected defines. Change to gateway-style version resolution:

- If `SOFTWARE_VERSION_MAJOR` is defined (CI passes all three), build
  `PROJECT_VER` from the three values (`"<maj>.<min>.<patch>"`).
- Else fall back to `git describe --tags --abbrev=0 --match "V*"`, parse
  `^V0*([0-9]+)\.0*([0-9]+)\.0*([0-9]+)$` (uppercase V, leading zeros stripped),
  and build `PROJECT_VER` from that; final fallback `0.0.0-dev`.
- `project(KCThermostat)` (was `Strux`).

No `SOFTWARE_ID` define, no `SOFTWARE_VERSION*` compile definitions — firmware
version continues to surface via `PROJECT_VER` →
`esp_app_get_description()->version`, exactly as it does now. This keeps I1 out
of the firmware while giving CI a clean way to stamp the version.

## I3 — Branding

Replace every Strux identity string. `KC Thermostat` is the friendly display
name; hostname surfaces derive a sanitized `kc-thermostat`.

| Surface | File | From | To |
|---|---|---|---|
| `device.name` default | `main/Application/SystemManager/SystemManager.h` | `"Strux"` | `"KC Thermostat"` |
| `GetDeviceName` fallback | same | `"Strux"` | `"KC Thermostat"` |
| mDNS hostname / instance | `main/Application/NetworkManager/NetworkManager.cpp` | device name verbatim | sanitized → `kc-thermostat.local` |
| AP SSID default | `main/Application/NetworkManager/NetworkManager.h` | `"Strux-AP"` | `"KC Thermostat-AP"` |
| CMake project/binary | `CMakeLists.txt` | `project(Strux)` | `project(KCThermostat)` |
| Login product name | `frontend/src/config.ts` `PRODUCT_NAME` | `"Strux"` | `"KC Thermostat"` |
| Dev proxy host | `frontend/src/config.ts` `DEV_HOST` | `"strux.local"` | `"kc-thermostat.local"` |
| Browser tab title | `frontend/index.html` `<title>` | `"Device"` | `"KC Thermostat"` |
| Project docs | `README.md` | Strux template | rebranded to KC Thermostat |

### mDNS hostname sanitization

`device.name` is user-editable and now defaults to a value containing a space,
which is invalid in an mDNS/DNS hostname. `NetworkManager` currently passes the
device name verbatim to `mdns_hostname_set` / `mdns_instance_name_set` /
`esp_netif_set_hostname`. Add a small local sanitizer (in NetworkManager, where
the hostname is built) that maps the device name to a hostname-safe label:
lowercase, spaces and any non-`[a-z0-9-]` char → `-`, collapse repeats, trim
leading/trailing `-`, non-empty fallback. `"KC Thermostat"` → `kc-thermostat`.
The **instance name** (the human-facing mDNS label) keeps the friendly device
name; only the **hostname** is sanitized. `esp_netif` hostname also uses the
sanitized form.

Scope guard: the sanitizer is the only new logic; it lives beside the existing
mDNS setup, not a new module. Visual design of the web UI is explicitly out of
scope (per the backlog).

### Rip out the GitHub update-check

The "newer release available" indicator polls
`api.github.com/repos/<GITHUB_REPO>/releases/latest`. The real origin
(`KooleControls/Thermostat`) is **private**, so the check cannot work; rather
than point it at a dead/authless URL, remove the feature.

- Delete `frontend/src/hooks/use-latest-release.ts` (exists only for this poll).
- `frontend/src/components/AppSidebar.tsx`: remove the `useLatestRelease` and
  `isNewerVersion` imports, the `release` / `updateAvailable` locals, and the
  green update-dot span on the firmware nav item.
- `frontend/src/lib/version.ts`: remove `isNewerVersion` (sidebar was its only
  user). **Keep `isPreRelease`** — the Pre-release badge is driven by the
  device's own firmware version (patch ≠ 0), independent of GitHub.
- `frontend/src/config.ts`: remove the `GITHUB_REPO` constant.

The Pre-release badge, the Version footer line, and the Firmware OTA-upload page
are unaffected — none depend on the GitHub poll.

## Verification

No automated tests (per repo convention). Verify by building:

- `cd frontend && pnpm typecheck && pnpm build` — clean after the update-check
  removal and config/title changes.
- `idf.py set-target esp32s3 && idf.py -DBOARD=diyless_thermostat_3 build` —
  clean; binary emits as `KCThermostat.bin`; `PROJECT_VER` still resolves.
- Sanity-check CI version injection locally:
  `idf.py -DSOFTWARE_VERSION_MAJOR=1 -DSOFTWARE_VERSION_MINOR=2 -DSOFTWARE_VERSION_PATCH=3 build`
  → device reports `1.2.3`.
- On hardware (optional, later): AP SSID shows `KC Thermostat-AP`; device
  resolves at `kc-thermostat.local`; web UI title/login show `KC Thermostat`.
- The release workflow itself is verified by pushing a `V*` tag (or
  `workflow_dispatch`) and confirming the three correctly-named artifacts attach
  to the release with the right prerelease flag.

## Out of scope

- I1 `software-id` (SID embedded in firmware + reported + wiki reservation).
- `.hex` / `.kczip` service-tool artifacts (parked; add if service tooling needs them).
- Web-UI visual redesign.
- Gateway-side identity for the thermostat (deferred per roadmap).
