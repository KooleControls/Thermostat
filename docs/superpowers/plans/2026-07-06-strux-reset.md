# Strux Reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reset the Thermostat repo to pure current Strux on a new branch `feature/strux-reset`, with only the spec/plan docs added, verified by a green build and pushed to origin.

**Architecture:** The Thermostat repo shares git history with the local Strux repo (`C:\Workspace\Strux`, added as remote `strux`). We create a branch pointing exactly at `strux/main`, commit the two `docs/superpowers/` documents onto it, prove it builds (Strux default: target `esp32`, board `esp32_devkit`, frontend auto-built by CMake via pnpm), and push. No thermostat code is carried over; `feature/ot-thermostat-dropin` keeps all prior work.

**Tech Stack:** git, ESP-IDF v6.0 (EIM install), pnpm/Node 22 (React frontend).

## Global Constraints

- Repo: `c:\Workspace\KC1245 Gateway workspace\Thermostat` (origin = `https://github.com/KooleControls/Thermostat.git`).
- Do NOT touch `main` or `feature/ot-thermostat-dropin`.
- ESP-IDF activation on this machine: dot-source `C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1` in PowerShell. Do NOT use `C:\esp\v6.0\esp-idf\export.ps1` (broken python env).
- Set `$env:PYTHONUTF8="1"` and `$env:PYTHONIOENCODING="utf-8"` before any `idf.py` call (Unicode output crashes idf.py on the cp1252 console).
- Strux baseline builds for target `esp32` (NOT `esp32s3` — that was the thermostat board). The existing `build/` dir contains stale esp32s3/thermostat CMake caches and must be deleted, not fullclean-ed.
- Commit messages end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: Create `feature/strux-reset` at `strux/main` and commit the docs

**Files:**
- Create (already on disk, untracked — they survive the branch switch):
  - `docs/superpowers/specs/2026-07-06-strux-reset-design.md`
  - `docs/superpowers/plans/2026-07-06-strux-reset.md`

**Interfaces:**
- Consumes: remote `strux` → `C:\Workspace\Strux` (already added and fetched).
- Produces: local branch `feature/strux-reset` = `strux/main` + one docs commit. Tasks 2–3 run on this branch.

- [ ] **Step 1: Verify the working tree is clean and the remote is fetched**

Run (Git Bash, from `c:\Workspace\KC1245 Gateway workspace\Thermostat`):

```bash
git status --porcelain
git fetch strux main
git log --oneline -1 strux/main
```

Expected: `git status` lists ONLY the two untracked `docs/superpowers/...` files (lines starting `??`). If any other modified/staged files appear, STOP and report. Last line shows strux/main's head commit (currently `48cca34 Polish: shadcn AlertDialog for reboot, ...`).

- [ ] **Step 2: Create the branch at strux/main**

```bash
git checkout -b feature/strux-reset strux/main
```

Expected: `Switched to a new branch 'feature/strux-reset'`. The untracked docs files remain in place.

- [ ] **Step 3: Verify the tree is byte-for-byte strux/main**

```bash
git diff strux/main --stat
git status --porcelain
```

Expected: `git diff` prints nothing (no tracked differences); `git status` shows only the two `??` docs files.

- [ ] **Step 4: Commit the spec and plan**

```bash
git add docs/superpowers/specs/2026-07-06-strux-reset-design.md docs/superpowers/plans/2026-07-06-strux-reset.md
git commit -m "docs: Strux reset spec + plan (repo reset to pure strux/main)

Thermostat features will be rebuilt one by one per the spec's backlog.
Prior demo work remains on feature/ot-thermostat-dropin.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

Expected: commit succeeds; `2 files changed` insertions only.

- [ ] **Step 5: Verify acceptance criterion**

```bash
git diff strux/main --name-only
```

Expected output is exactly the two `docs/superpowers/...` paths, nothing else.

### Task 2: Verify the Strux baseline builds (frontend + firmware)

**Files:**
- Modify: none committed. Generated: `build/` (untracked), `frontend/node_modules`, `www/` (frontend output). `sdkconfig` is regenerated for esp32 — check afterwards that git still reports a clean tree; if `sdkconfig` is tracked and modified, restore it with `git checkout -- sdkconfig` only if the build succeeded and the diff is target-related churn (report it either way).

**Interfaces:**
- Consumes: branch `feature/strux-reset` from Task 1.
- Produces: proven-green build; nothing new committed.

- [ ] **Step 1: Remove the stale thermostat build directory**

Run (PowerShell, from `c:\Workspace\KC1245 Gateway workspace\Thermostat`):

```powershell
if (Test-Path build) { Remove-Item -Recurse -Force build }
```

Expected: `build/` gone. (Stale caches are esp32s3 + old paths; `idf.py fullclean` refuses them.)

- [ ] **Step 2: Build the frontend standalone first (faster feedback than inside CMake)**

```powershell
Set-Location frontend; pnpm install; pnpm build; Set-Location ..
```

Expected: `pnpm build` ends with vite `✓ built in ...` and no errors.

- [ ] **Step 3: Activate ESP-IDF and build the firmware**

```powershell
$env:PYTHONUTF8 = "1"; $env:PYTHONIOENCODING = "utf-8"
. "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"
idf.py set-target esp32
idf.py build
```

Expected: `set-target esp32` regenerates `sdkconfig`; `idf.py build` finishes with `Project build complete.` and a binary named per Strux's CMake (check the final "Successfully created esp32 image" lines). If the console crashes with a UnicodeEncodeError, the UTF-8 env vars were not set — set them and retry.

- [ ] **Step 4: Confirm the git tree is still clean**

```powershell
git status --porcelain
```

Expected: empty, or only untracked generated dirs (`build/`, `www/`, `frontend/node_modules/` are gitignored in Strux — anything else appearing must be reported).

### Task 3: Push the branch to origin

**Files:** none.

**Interfaces:**
- Consumes: branch `feature/strux-reset` with green build.
- Produces: `origin/feature/strux-reset` on KooleControls/Thermostat.

- [ ] **Step 1: Push with upstream tracking**

```bash
git push -u origin feature/strux-reset
```

Expected: `* [new branch] feature/strux-reset -> feature/strux-reset` and tracking set. Push goes to `KooleControls/Thermostat` (verify the remote URL in the output — never a vanBassum remote).

- [ ] **Step 2: Final acceptance check**

```bash
git log --oneline -3
git diff origin/feature/strux-reset..HEAD
```

Expected: top commit is the docs commit, its parent is strux/main's head (`48cca34...`); the diff against origin is empty.
