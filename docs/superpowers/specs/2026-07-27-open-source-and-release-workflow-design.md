# Open-source the thermostat + release workflow — design

**Date:** 2026-07-27
**Status:** approved (brainstorm + audit complete)
**Jira:** RA2-395 (parent), decisions cross-linked on RA2-437 and RA2-442
**Backlog:** `docs/backlog/2026-07-27-open-source-public-repo.md`

> **Audit finding (2026-07-27): most of this step is already built.** The merged
> 2026-07-13 release-workflow + branding work (`docs/superpowers/specs/2026-07-13-release-workflow-and-branding-design.md`)
> already provides the tag-driven `release.yml`, full KC-Thermostat branding, and
> the CMake project rename. The remaining step-1 work is therefore small and
> mechanical (see *Work breakdown* below), not a from-scratch build.

## Goal

Take the KC Thermostat repository public under a clean, permissive licence, and
stand up a tag-driven CI workflow that builds the firmware and publishes a
versioned `.bin` to the GitHub Releases page. The Releases page becomes the host
that the remote-update work (RA2-437, and the display update UI) pulls pinned
builds from.

This is **step 1** of a four-step roadmap:

1. **Open-source + release workflow** ← this spec
2. Self-update: pull a pinned build from the GitHub Releases page over WiFi
3. WiFi-connect UI on the display
4. Firmware-update UI on the display

Steps 2–4 depend on this one: step 2 has nothing to pull from until Releases
exists, and the boundary/licence posture set here governs everything after.

## Non-goals

- The self-update trigger, WiFi-connect UI, and display update screen (steps 2–4).
- The ESPHome control-loop rebuild — a separate item. It only *couples* here via
  licence: adopting ESPHome's GPL source would force a later GPLv3 migration.
- Any change to the gateway. This step is thermostat-repo hygiene + CI only.

## Decisions

These were settled during the brainstorm and are recorded on RA2-395:

- **Scope:** publish the repo **and** build the release workflow. Publishing
  without the workflow would leave step 2 with no host.
- **Licence: MIT.** A single `LICENSE` file at the repo root, **no per-file
  headers** — the source stays clean. MIT is a real permissive open-source
  licence (unlike "no licence", which is all-rights-reserved and would make a
  public repo legally unusable). GPLv3 remains a *later* migration option, taken
  only if and when the ESPHome control-loop source is adopted; MIT → GPLv3 is the
  easy direction, and the control-loop item owns that trigger. If outside
  contributions land before any such migration, contributor consent is required —
  note this if it becomes relevant.
- **Publish strategy: flip the existing repo, keep history.** The audit found no
  secrets or KC-proprietary code anywhere in the working tree or the 313-commit
  history, so `KooleControls/Thermostat` is made public **with history intact** —
  preserving Strux attribution and provenance. A fresh squashed repo was
  considered and rejected: it adds work with no security benefit here.
- **Software ID: dropped.** The thermostat is treated as a standalone product, as
  if bought from a third party, so no KC software ID (SID 28) is embedded. This
  closes the open piece of `docs/backlog/2026-07-06-software-id.md`. Version and
  system-info reporting keep the git-tag-derived firmware version and simply carry
  no KC identifier. **Consequence for artifact naming:** the existing `release.yml`
  bakes `SID 28` and a `KC` prefix into the artifact filenames
  (`01_00_00_28_KC_Thermostat-…`); both are dropped in favour of standalone
  naming (`Thermostat-<version>-…`) so no KC identifiers appear anywhere.
- **Update host: GitHub Releases.** Pinned per-tag assets with stable,
  anonymously downloadable URLs. GitHub Actions *artifacts* are CI-internal only
  (ephemeral, auth-gated even on public repos) and are never the public pull
  target. Note: the frontend's GitHub "newer release available" check was removed
  earlier *because the repo was private*; open-source reverses that premise, but
  re-introducing a version check is **deferred to step 2** (self-update), where it
  belongs — out of scope here.
- **Internal docs: kept, links stripped.** `CLAUDE.md` and `docs/backlog/` stay
  in the public repo as useful development context, but private
  `koolecontrolsdevelopment.atlassian.net` links and bare `RA2-*` Jira keys are
  removed or neutralised as part of the audit step. They are not secrets, but
  they need not be exposed.

## Audit outcome (2026-07-27)

The pre-publish audit is **complete** and is the reason the publish strategy is
already resolved above.

- **Secrets/keys — clean.** No private keys, certs, or cloud tokens in the working
  tree or the 313-commit history; no `.pem`/`.key`/`.env` files. `web.password`
  defaults to empty; all "password/token" matches are legitimate runtime auth
  code, not hardcoded credentials. One placeholder (`PASSWORD="admin"`,
  `HOST="DEVICE_IP"`) in a plan doc is an example snippet, not a real secret.
- **KC-proprietary / boundary — clean.** No connection-server protocol, RC4/AES
  keys, or fleet semantics. MQTT/HA references are doc-comment examples only; no
  managers. The `CommandManager` surface is generic.
- **Already built.** `release.yml`, branding, and the CMake rename are merged
  (2026-07-13 work).
- **Strip-list (non-secret internal references).** Private
  `koolecontrolsdevelopment.atlassian.net` links and bare `RA2-*` keys appear in
  `CLAUDE.md`, `docs/backlog/*.md`, `docs/superpowers/**`, and two comments in
  `main/hardware/drivers/Stm32OpenThermLink.h`. `README.md` is clean. These are
  cosmetic, not sensitive.

## Work breakdown (remaining)

Because the release workflow and branding already exist, what remains is small
and mechanical.

### 1. Standalone artifact naming

Edit `.github/workflows/release.yml` to drop the `SOFTWARE_ID` and
`ARTIFACT_PREFIX` env constants and rebuild the artifact base as
`<PROJECT_NAME>-<version>` (e.g. `Thermostat-1.0.0-factory.bin`,
`Thermostat-1.0.0.bin`, `Thermostat-1.0.0-www.bin`), updating the release-body
text to match. No KC identifiers in any filename.

### 2. Close out the software-ID item

Update `docs/backlog/2026-07-06-software-id.md` to record the item as closed
"dropped" (standalone product), not "embedded". No firmware change is needed —
the SID was never embedded, only present in the old artifact name (removed in
task 1).

### 3. MIT licence

Add a single MIT `LICENSE` file at the repo root — no per-file headers. Copyright
holder to be confirmed with the user (Koole Controls vs. individual).

### 4. Repo hygiene for outside eyes

Strip/neutralise the private Jira/Confluence references identified by the audit
from the tracked docs and the two `Stm32OpenThermLink.h` comments. Confirm
`README.md` reads as outward-facing (what the device is, DIYLESS T3 board, the
`pnpm build` + `idf.py set-target esp32s3 && idf.py build` flow) and preserves
Strux upstream attribution.

### 5. Publish (user action)

Flip `KooleControls/Thermostat` to public with history intact. This is a
GitHub-side action the user performs; keep the `strux` upstream relationship
intact.

### 6. Verify the release path

Push a `V*` tag (or `workflow_dispatch`) and confirm a GitHub Release appears with
the three correctly-named standalone artifacts and the right prerelease flag — the
host step 2 will pull pinned builds from.

## Interfaces and boundaries

The only new external surface this step creates is the **GitHub Releases page**:
a set of per-tag `.bin` assets at stable URLs. Step 2's self-update consumes
exactly this — a URL naming a specific version — through the already-existing
`updateFromUrl` command. Nothing about the device's runtime command surface
changes here.

The binding boundary rule is reaffirmed, not newly created: no KC-proprietary
knowledge lives in this repo. The audit is what verifies it actually holds before
the code becomes visible.

## Verification

- Audit produces a written finding (clean / items-to-scrub) and the publish-strategy choice.
- Repo is public with an MIT `LICENSE`, a build-instruction `README`, and no private links in tracked docs.
- No KC software ID is present in a built image; version/system-info still reports the firmware version.
- Pushing a `V*` tag produces a GitHub Release with a correctly-named, version-pinned `.bin` that flashes and boots on a DIYLESS T3.
