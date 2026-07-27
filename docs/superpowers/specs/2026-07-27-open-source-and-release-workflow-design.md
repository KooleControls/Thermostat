# Open-source the thermostat + release workflow — design

**Date:** 2026-07-27
**Status:** approved (brainstorm complete)
**Jira:** RA2-395 (parent), decisions cross-linked on RA2-437 and RA2-442
**Backlog:** `docs/backlog/2026-07-27-open-source-public-repo.md`

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
- **Publish strategy: audit first.** The secret/history + boundary audit runs
  before choosing how to publish. Its result decides between (a) flipping the
  existing `KooleControls/Thermostat` repo public with history intact, and
  (b) pushing a clean tree to a fresh public repo with squashed history. Do not
  pre-commit to either — the audit is the input.
- **Software ID: dropped.** The thermostat is treated as a standalone product, as
  if bought from a third party, so no KC software ID (SID 28) is embedded. This
  closes the open piece of `docs/backlog/2026-07-06-software-id.md`. Version and
  system-info reporting keep the git-tag-derived firmware version and simply carry
  no KC identifier.
- **Update host: GitHub Releases.** Pinned per-tag assets with stable,
  anonymously downloadable URLs. GitHub Actions *artifacts* are CI-internal only
  (ephemeral, auth-gated even on public repos) and are never the public pull
  target.
- **Internal docs: kept, links stripped.** `CLAUDE.md` and `docs/backlog/` stay
  in the public repo as useful development context, but private
  `koolecontrolsdevelopment.atlassian.net` links and bare `RA2-*` Jira keys are
  removed or neutralised as part of the audit step. They are not secrets, but
  they need not be exposed.

## Work breakdown

The step is a sequence with one hard gate (the audit) that determines how the
later publish is done.

### 1. Audit (gate)

Scan the **working tree and the full git history** for anything that must not go
public: secrets, keys, private endpoints, and any KC-proprietary code (the
connection-server protocol, fleet semantics, encryption keys). Confirm the
`CommandManager` surface is genuinely generic — no KC meaning has leaked into the
thermostat. In parallel, inventory the internal references
(`koolecontrolsdevelopment.atlassian.net` links, `RA2-*` keys) that the
docs-cleanup task will strip.

The audit's outcome is the deciding input for the publish strategy: a clean
history supports flipping the existing repo public; a dirty history that cannot
be cheaply rewritten pushes toward a fresh, squashed public repo. Record the
finding and the resulting choice.

### 2. Remove the software-ID concept

Follow the standalone-product framing: remove any KC software-ID scaffolding so
none is embedded, and verify version/system-info still reports a sane
git-tag-derived firmware version with no KC identifier. Update
`docs/backlog/2026-07-06-software-id.md` to reflect that the item is closed as
"dropped", not "embedded".

### 3. Licence + repo hygiene

Add an MIT `LICENSE` file at the repo root (no per-file headers). Write an
outward-facing `README` covering what the device is, the board target (DIYLESS
Thermostat 3), and the build (`pnpm build` for the frontend, then
`idf.py set-target esp32s3 && idf.py build`). Preserve the Strux upstream
attribution and the `strux` remote relationship. Strip the private Jira/Confluence
links identified in the audit from `CLAUDE.md` and `docs/backlog/`.

### 4. Publish

Execute the strategy chosen from the audit (flip existing repo, or push scrubbed
tree to a fresh public repo). If a fresh repo is used, re-establish the Strux
upstream link and attribution.

### 5. Release workflow

A GitHub Actions workflow triggered on a `V*` tag push:

1. Build the frontend (`pnpm build`), which gzips into the `www` FAT image.
2. Inject the version from the tag into the build
   (`-DSOFTWARE_VERSION_MAJOR/MINOR/PATCH`), matching the existing tag-derived
   versioning in the root `CMakeLists.txt`.
3. `idf.py build` for the ESP32-S3 / DIYLESS T3 target.
4. Attach the versioned `.bin` to a GitHub Release for that tag.

The build `.bin` may move between jobs as an Actions artifact internally, but the
**public** deliverable is the Release asset. Naming carries the version so a
consumer can pin a specific build (the RA2-437 hard requirement: never "just
latest").

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
