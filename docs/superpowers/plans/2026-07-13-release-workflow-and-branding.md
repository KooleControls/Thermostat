# Release Workflow + Branding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the thermostat a gateway-style release workflow and replace all Strux branding with "KC Thermostat" identity.

**Architecture:** Two independent infra items sharing a naming convention. The GitHub Actions `release.yml` is rewritten to parse `VX.Y.Z` tags, inject version defines, and publish three renamed bins; the root `CMakeLists.txt` learns to accept those defines. Branding is a set of string swaps plus a small mDNS hostname sanitizer and the removal of the (now-dead, private-repo) GitHub update-check.

**Tech Stack:** GitHub Actions, ESP-IDF v6.0 CMake, C++17 (ESP-IDF), React 19 + TypeScript + Vite (pnpm).

## Global Constraints

- **No automated tests in this repo.** Verification is building: `pnpm typecheck && pnpm build` for the frontend, `idf.py build` for firmware. Every task ends with the relevant build gate + a commit.
- **KC numbers are hardware only.** Firmware/CMake project/binary name is plain `Thermostat` (→ `Thermostat.bin`); the `KC` in artifact names comes from `ARTIFACT_PREFIX`. Never name firmware `KC1247-*` or `KCThermostat`.
- **Friendly display name is `KC Thermostat`** (with a space). Hostname surfaces (mDNS, esp_netif) use the sanitized `kcthermostat` (lowercase, spaces/punctuation stripped — no dash).
- **Software ID 28 appears in the release filename only** (`MM_mm_pp_28_KC_Thermostat`). Do **not** add a `SOFTWARE_ID` compile define or report the SID in firmware — backlog item I1 (firmware SID embedding) stays deferred pending the open-source decision.
- **Version tags are uppercase `VX.Y.Z`** (Koole/gateway convention). `patch != 0 ⇒ prerelease`.
- **C++ style:** `snprintf` with `sizeof` bounds; no `strcpy`/`strcat`.
- **Branch:** work lands on `feature/release-workflow-and-branding` (already checked out).

---

### Task 1: CMake — rename project + accept injected version defines

**Files:**
- Modify: `CMakeLists.txt:5-18` (version block) and `:38` (`project()`)

**Interfaces:**
- Consumes: nothing.
- Produces: binary basename `Thermostat.bin` (Task 5 copies `build/Thermostat.bin`); `PROJECT_VER` honoring `-DSOFTWARE_VERSION_MAJOR/MINOR/PATCH` (Task 5 passes these).

- [ ] **Step 1: Replace the version-resolution block**

In `CMakeLists.txt`, replace lines 5-18 (the current `# Derive version from git tag ...` block through its `endif()`) with:

```cmake
# Version resolution:
#   CI passes -DSOFTWARE_VERSION_MAJOR/MINOR/PATCH parsed from the VX.Y.Z tag.
#   Local builds derive it from the nearest uppercase-V tag (git describe);
#   fall back to 0.0.0-dev when git or a matching tag is unavailable.
if(DEFINED SOFTWARE_VERSION_MAJOR)
    set(PROJECT_VER "${SOFTWARE_VERSION_MAJOR}.${SOFTWARE_VERSION_MINOR}.${SOFTWARE_VERSION_PATCH}")
else()
    execute_process(
        COMMAND git describe --tags --abbrev=0 --match "V*"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )
    if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^V0*([0-9]+)\\.0*([0-9]+)\\.0*([0-9]+)$")
        set(PROJECT_VER "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    else()
        set(PROJECT_VER "0.0.0-dev")
    endif()
endif()
```

- [ ] **Step 2: Rename the project**

Change line 38 from:

```cmake
project(Strux)
```

to:

```cmake
project(Thermostat)
```

- [ ] **Step 3: Build to verify (local dev build)**

Run:
```bash
idf.py set-target esp32s3
idf.py -DBOARD=diyless_thermostat_3 build
```
Expected: build succeeds; `build/Thermostat.bin` exists (the basename changed from `Strux.bin`).

- [ ] **Step 4: Verify version injection works**

Run:
```bash
idf.py -DBOARD=diyless_thermostat_3 -DSOFTWARE_VERSION_MAJOR=1 -DSOFTWARE_VERSION_MINOR=2 -DSOFTWARE_VERSION_PATCH=3 reconfigure
grep -R "PROJECT_VER" build/CMakeCache.txt || true
```
Expected: reconfigure succeeds. (The value surfaces at runtime via `esp_app_get_description()->version` = `1.2.3`; the reconfigure succeeding without error is the gate here.)

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: rename project to Thermostat; accept -DSOFTWARE_VERSION_* (uppercase V tags)"
```

---

### Task 2: Firmware branding — defaults + mDNS hostname sanitizer

**Files:**
- Modify: `main/Application/SystemManager/SystemManager.h:41` (device.name default)
- Modify: `main/Application/NetworkManager/NetworkManager.h:20` (AP SSID default)
- Modify: `main/Application/NetworkManager/NetworkManager.cpp:50-58` (sanitize hostname; keep friendly instance name)

**Interfaces:**
- Consumes: `SystemManager::GetDeviceName(char*, size_t)` (unchanged signature).
- Produces: device advertises `kcthermostat.local`; friendly name `KC Thermostat` still shown as the mDNS instance name and router hostname source.

Note: `GetDeviceName`'s empty-value fallback uses `esp_app_get_description()->project_name` (now `Thermostat`) — that is fine and needs no change; the friendly default lives in the `name_` setting below.

- [ ] **Step 1: Change the device.name default**

In `main/Application/SystemManager/SystemManager.h`, line 41, change:

```cpp
    inline static StringSetting name_{ "device.name", "Device Name", "Strux" };
```
to:
```cpp
    inline static StringSetting name_{ "device.name", "Device Name", "KC Thermostat" };
```

- [ ] **Step 2: Change the AP SSID default**

In `main/Application/NetworkManager/NetworkManager.h`, line 20, change:

```cpp
    static constexpr const char* DefaultApSsid = "Strux-AP";
```
to:
```cpp
    static constexpr const char* DefaultApSsid = "KC Thermostat-AP";
```

- [ ] **Step 3: Add a file-local hostname sanitizer in NetworkManager.cpp**

In `main/Application/NetworkManager/NetworkManager.cpp`, add this helper just above `NetworkManager::Init()` (after the includes / `TAG` definition, before the first method):

```cpp
// mDNS/DNS hostnames may not contain spaces or punctuation. Derive a safe
// label from the friendly device name: lowercase, keep [a-z0-9], drop the
// rest. "KC Thermostat" -> "kcthermostat". Falls back to "thermostat".
static void SanitizeHostname(const char* in, char* out, size_t maxLen)
{
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < maxLen; ++i)
    {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c >= 'A' && c <= 'Z')
            out[j++] = static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out[j++] = static_cast<char>(c);
        // everything else (spaces, punctuation) is dropped
    }
    out[j] = '\0';
    if (out[0] == '\0')
        snprintf(out, maxLen, "thermostat");
}
```

- [ ] **Step 4: Use the sanitized hostname for hostname surfaces, keep friendly instance name**

In the same file, replace the block at lines 50-58:

```cpp
    // Set hostname from the device name so it shows in the router
    char deviceName[33] = {};
    serviceProvider_.getSystemManager().GetDeviceName(deviceName, sizeof(deviceName));
    wifi_interface_.SetHostname(deviceName);

    // mDNS — <deviceName>.local
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(deviceName);
    mdns_instance_name_set(deviceName);
```

with:

```cpp
    // Friendly name (router label + human-facing mDNS instance) and a
    // hostname-safe form derived from it (mDNS/DNS host label).
    char deviceName[33] = {};
    serviceProvider_.getSystemManager().GetDeviceName(deviceName, sizeof(deviceName));
    char hostName[33] = {};
    SanitizeHostname(deviceName, hostName, sizeof(hostName));
    wifi_interface_.SetHostname(hostName);

    // mDNS — <hostName>.local, advertised under the friendly instance name
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(hostName);
    mdns_instance_name_set(deviceName);
```

(Line 59, `mdns_service_add(...)`, is unchanged.)

- [ ] **Step 5: Build to verify**

Run:
```bash
idf.py -DBOARD=diyless_thermostat_3 build
```
Expected: build succeeds with no warnings from the edited files.

- [ ] **Step 6: Commit**

```bash
git add main/Application/SystemManager/SystemManager.h main/Application/NetworkManager/NetworkManager.h main/Application/NetworkManager/NetworkManager.cpp
git commit -m "feat(branding): KC Thermostat device name + AP SSID; sanitize mDNS host to kcthermostat.local"
```

---

### Task 3: Frontend — rip out the GitHub update-check

**Files:**
- Delete: `frontend/src/hooks/use-latest-release.ts`
- Modify: `frontend/src/components/AppSidebar.tsx` (remove imports, locals, update-dot)
- Modify: `frontend/src/lib/version.ts` (remove `isNewerVersion`, keep `isPreRelease`)
- Modify: `frontend/src/config.ts` (remove `GITHUB_REPO`)

**Interfaces:**
- Consumes: nothing new.
- Produces: `config.ts` no longer exports `GITHUB_REPO`; `version.ts` still exports `isPreRelease`.

The private `KooleControls/Thermostat` repo makes the "newer release available" poll impossible, so the feature is removed rather than repointed. The Pre-release badge (driven by the device's own version) stays.

- [ ] **Step 1: Delete the hook**

```bash
git rm frontend/src/hooks/use-latest-release.ts
```

- [ ] **Step 2: Remove update-check usage from the sidebar**

In `frontend/src/components/AppSidebar.tsx`:

Delete these two import lines (23-24) — **keep** the `PreReleaseBadge` import on line 25:
```ts
import { useLatestRelease } from "@/hooks/use-latest-release"
import { isNewerVersion } from "@/lib/version"
```

Delete the two locals (lines 58-59):
```ts
  const release = useLatestRelease()
  const updateAvailable = info && release && isNewerVersion(info.firmware, release.version)
```

Delete the update-dot inside the nav map (lines 86-88):
```tsx
                    {item.page === "firmware" && updateAvailable && (
                      <span className="ml-auto h-2 w-2 rounded-full bg-emerald-500" />
                    )}
```

- [ ] **Step 3: Remove `isNewerVersion` from version.ts**

In `frontend/src/lib/version.ts`, delete the `isNewerVersion` function (lines 8-18), leaving only `isPreRelease`. The file becomes:

```ts
export function isPreRelease(version: string | undefined): boolean {
  if (!version) return false
  const parts = version.split(".")
  const patch = parts[2]
  return patch !== undefined && patch !== "0"
}
```

- [ ] **Step 4: Remove GITHUB_REPO from config.ts**

In `frontend/src/config.ts`, delete these lines:
```ts
/** GitHub repo checked for new releases (update dot in the sidebar). */
export const GITHUB_REPO = "vanBassum/Strux"
```

- [ ] **Step 5: Typecheck + build**

Run:
```bash
cd frontend && pnpm typecheck && pnpm build
```
Expected: clean — no unused-import or missing-export errors, `../www` regenerated.

- [ ] **Step 6: Commit**

```bash
git add -A frontend/src
git commit -m "feat(web): remove GitHub update-check (private repo); keep pre-release badge"
```

---

### Task 4: Frontend — branding strings

**Files:**
- Modify: `frontend/src/config.ts` (`PRODUCT_NAME`, `DEV_HOST`)
- Modify: `frontend/index.html:7` (`<title>`)

**Interfaces:**
- Consumes: `config.ts` (already edited in Task 3; no `GITHUB_REPO`).
- Produces: login/product name `KC Thermostat`; dev proxy target `kcthermostat.local`.

- [ ] **Step 1: Update config.ts strings**

In `frontend/src/config.ts`, change:
```ts
export const DEV_HOST = "strux.local"
```
to:
```ts
export const DEV_HOST = "kcthermostat.local"
```
and:
```ts
export const PRODUCT_NAME = "Strux"
```
to:
```ts
export const PRODUCT_NAME = "KC Thermostat"
```

- [ ] **Step 2: Update the static tab title**

In `frontend/index.html`, line 7, change:
```html
    <title>Device</title>
```
to:
```html
    <title>KC Thermostat</title>
```
(The runtime code still overwrites this with the live device name post-auth; this is the pre-load fallback.)

- [ ] **Step 3: Typecheck + build**

Run:
```bash
cd frontend && pnpm typecheck && pnpm build
```
Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/config.ts frontend/index.html
git commit -m "feat(branding): KC Thermostat product name, dev host, tab title"
```

---

### Task 5: Rewrite the release workflow (gateway-style)

**Files:**
- Modify (full rewrite): `.github/workflows/release.yml`

**Interfaces:**
- Consumes: `build/Thermostat.bin` (Task 1), `-DSOFTWARE_VERSION_*` support (Task 1), `-DBOARD` support (existing), `build/www.bin` (existing FAT image from the frontend build).
- Produces: three release assets named `MM_mm_pp_28_KC_Thermostat{-factory,,-www}.bin`.

- [ ] **Step 1: Replace the workflow file**

Overwrite `.github/workflows/release.yml` with:

```yaml
name: Release (ESP-IDF)

on:
  push:
    tags:
      - "V*"
  workflow_dispatch: {}

permissions:
  contents: write

# Easy-to-change constants
env:
  PROJECT_NAME: "Thermostat"
  IDF_VERSION: "v6.0"
  IDF_TARGET: "esp32s3"
  BOARD: "diyless_thermostat_3"
  SOFTWARE_ID: "28"
  ARTIFACT_PREFIX: "KC"

jobs:
  build-and-release:
    runs-on: ubuntu-latest

    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Parse version from tag
        id: version
        shell: bash
        run: |
          set -euo pipefail
          TAG="${GITHUB_REF_NAME}"
          if [[ ! "$TAG" =~ ^V([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
            echo "Tag '$TAG' does not match expected format VX.Y.Z" >&2
            exit 1
          fi

          echo "major=${BASH_REMATCH[1]}" >> "$GITHUB_OUTPUT"
          echo "minor=${BASH_REMATCH[2]}" >> "$GITHUB_OUTPUT"
          echo "patch=${BASH_REMATCH[3]}" >> "$GITHUB_OUTPUT"

          VERSION_STR="$(printf "%02d_%02d_%02d" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}")"
          ARTIFACT_BASE="${VERSION_STR}_${SOFTWARE_ID}_${ARTIFACT_PREFIX}_${PROJECT_NAME}"
          echo "ARTIFACT_BASE=${ARTIFACT_BASE}" >> "$GITHUB_ENV"

          if [[ "${BASH_REMATCH[3]}" -eq 0 ]]; then
            echo "prerelease=false" >> "$GITHUB_OUTPUT"
          else
            echo "prerelease=true" >> "$GITHUB_OUTPUT"
          fi

      - name: Setup pnpm
        uses: pnpm/action-setup@v4
        with:
          version: 10

      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: 22
          cache: pnpm
          cache-dependency-path: frontend/pnpm-lock.yaml

      - name: Build frontend
        run: cd frontend && pnpm install --frozen-lockfile && pnpm build

      - name: Build firmware (ESP-IDF)
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: ${{ env.IDF_VERSION }}
          target: ${{ env.IDF_TARGET }}
          command: |
            set -euo pipefail
            idf.py set-target ${{ env.IDF_TARGET }}
            idf.py -DBOARD=${{ env.BOARD }} \
              -DSOFTWARE_VERSION_MAJOR=${{ steps.version.outputs.major }} \
              -DSOFTWARE_VERSION_MINOR=${{ steps.version.outputs.minor }} \
              -DSOFTWARE_VERSION_PATCH=${{ steps.version.outputs.patch }} \
              build
            cd build && esptool.py --chip ${{ env.IDF_TARGET }} merge_bin -o merged-factory.bin -f raw @flash_args

      - name: Prepare release artifacts
        shell: bash
        run: |
          set -euo pipefail
          mkdir -p dist

          # Full factory image (bootloader + partition table + app + www)
          cp build/merged-factory.bin "dist/${ARTIFACT_BASE}-factory.bin"

          # Application-only binary (web-UI OTA)
          cp "build/${PROJECT_NAME}.bin" "dist/${ARTIFACT_BASE}.bin"

          # WWW FAT partition image (independent web-UI OTA)
          cp build/www.bin "dist/${ARTIFACT_BASE}-www.bin"

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          prerelease: ${{ steps.version.outputs.prerelease }}
          generate_release_notes: true
          body: |
            ### Firmware files
            - **${{ env.ARTIFACT_BASE }}-factory.bin** — Full factory image (bootloader + partition table + app + www). Use for initial flashing or full recovery via `esptool.py`.
            - **${{ env.ARTIFACT_BASE }}.bin** — Application firmware only. Upload via the web UI (Firmware > Application Firmware) for OTA update.
            - **${{ env.ARTIFACT_BASE }}-www.bin** — Web interface FAT image. Upload via the web UI (Firmware > WWW Partition) to update the frontend independently.
          files: |
            dist/${{ env.ARTIFACT_BASE }}-factory.bin
            dist/${{ env.ARTIFACT_BASE }}.bin
            dist/${{ env.ARTIFACT_BASE }}-www.bin
```

- [ ] **Step 2: Lint the YAML**

Run:
```bash
python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml')); print('yaml ok')"
```
Expected: `yaml ok`.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: gateway-style release workflow (VX.Y.Z, SID 28 in name, bins only)"
```

- [ ] **Step 4: Post-merge verification note (not a code step)**

The workflow only truly runs on a `V*` tag or `workflow_dispatch`. After merge, trigger it once (e.g. `workflow_dispatch`, or push a `V0.0.1` test tag) and confirm three assets attach named `00_00_01_28_KC_Thermostat{-factory,,-www}.bin` with `prerelease=true` (patch=1). Leave this unchecked until that run is confirmed.

---

### Task 6: Rebrand README

**Files:**
- Modify: `README.md`

**Interfaces:** none (documentation only).

- [ ] **Step 1: Rewrite README front matter and Strux references**

Replace Strux-specific identity in `README.md` with KC Thermostat. Concretely:
- Title `# Strux` → `# KC Thermostat`.
- Opening description: reframe from "Strux is a flexible foundation for building embedded applications" to a one-paragraph description of the KC Thermostat — an OpenTherm room thermostat (OT master) that pairs with the KC1245 Gateway as a drop-in for the third-party unit, built on the Strux template.
- AP-fallback SSID mentions `Strux-AP` → `KC Thermostat-AP`.
- Release-asset filenames (`Strux-factory.bin`, `Strux-app.bin`, `Strux-www.bin`) → the new naming `MM_mm_pp_28_KC_Thermostat{-factory,,-www}.bin`, and the GitHub Releases link `vanBassum/Strux` → `KooleControls/Thermostat`.
- Directory-tree label `Strux/` → `Thermostat/`.
- Keep the "no MQTT/HA" note (accurate) but attribute smart-home to the gateway, matching `CLAUDE.md`.

Keep it factual and short; do not invent features. Preserve sections that are still accurate (build steps, architecture overview).

- [ ] **Step 2: Sanity-check no stray "Strux" identity remains where it shouldn't**

Run:
```bash
grep -n "Strux" README.md || echo "no Strux references"
```
Expected: the only acceptable remaining mentions are ones describing the upstream template lineage (e.g. "built on the Strux template"). Remove or reword any that present the product itself as Strux.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: rebrand README to KC Thermostat"
```

---

## Self-Review

**Spec coverage:**
- I2 release.yml rewrite → Task 5. ✓
- I2 CMake version-define support + uppercase-V → Task 1. ✓
- I2 bins-only artifacts, SID 28 in filename → Task 5 (no SID compile define — Task 1 deliberately omits it). ✓
- I3 device.name / AP SSID / mDNS sanitizer → Task 2. ✓
- I3 CMake project/binary name → Task 1. ✓
- I3 frontend PRODUCT_NAME / DEV_HOST / title → Task 4. ✓
- I3 rip out update-check (hook, sidebar, version.ts, GITHUB_REPO) → Task 3. ✓
- I3 README rebrand → Task 6. ✓
- Deferred I1 (firmware SID embedding) → explicitly out; no task adds it. ✓

**Placeholder scan:** No TBD/TODO; every code step shows the exact code or command. Task 5 Step 4 and the workflow's real run are described as a post-merge manual trigger (a genuine external gate, not a placeholder).

**Type/name consistency:** `Thermostat` used consistently for CMake project, binary basename, and workflow `PROJECT_NAME`. `SanitizeHostname(const char*, char*, size_t)` defined and used in Task 2 only. `isPreRelease` kept, `isNewerVersion`/`useLatestRelease`/`GITHUB_REPO` removed consistently across Task 3 (frontend). `ARTIFACT_BASE` defined in the version step and referenced in later workflow steps via `$GITHUB_ENV`.
