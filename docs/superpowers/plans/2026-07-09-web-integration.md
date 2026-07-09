# Web Integration (item 8) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Thermostat control page and a Diagnostics page to the web UI, driven entirely by the device's existing WebSocket commands.

**Architecture:** Frontend-only. Typed wrappers over the existing `send()` primitive expose `climateStatus`/`climateSet`, `hotWaterStatus`/`hotWaterSet`, and `otStatus`. Two per-page React hooks poll those commands while their page is mounted (the `use-device-info` pattern) and issue set-commands, reconciling from each reply. Two new pages are wired into the hand-rolled router.

**Tech Stack:** React 19 + TypeScript + Vite + Tailwind + shadcn/ui; pnpm; Lucide icons. No external state library — component state + a singleton `backend` service.

## Global Constraints

- **Frontend-only.** No firmware changes, no new device commands. Everything lives under `frontend/src/`.
- **No automated tests in this repo.** The per-task gate is `pnpm typecheck` (runs `tsc --noEmit`) from `frontend/`; the final task builds and drives a real device. Do NOT invent a test framework.
- **Import alias** is `@/` → `frontend/src/`.
- **Card idiom:** plain `div` with `className="rounded-xl border bg-card p-6 text-card-foreground shadow-sm"` (matches `HomePage.tsx`); do not add a shadcn `Card` component. Available shadcn components used here: `Button`, `Switch`, `Badge` (already present under `@/components/ui/`).
- **Command JSON field names are verbatim from firmware** — do not rename. Setpoint range/step: room `5–30` / `0.5`; DHW `30–80` / `1`. Setpoint writes debounce `500 ms`. `hotWaterSet.enable` is an integer on the wire (`1`/`0`; `-1`/absent = unchanged).
- **Run commands from `frontend/`:** `cd frontend && pnpm typecheck`.

---

### Task 1: Backend types and command wrappers

**Files:**
- Modify: `frontend/src/lib/backend.ts` (add three API methods inside the `// ── API methods ──` block near line 289; add three interfaces in the trailing `// ── Types ──` block after `PartitionsResponse`, ~line 558)

**Interfaces:**
- Consumes: existing `send<T>(type, params)` on `BackendService`.
- Produces (later tasks rely on these exact names/types):
  - `backend.getClimateStatus(): Promise<ClimateStatus>`
  - `backend.setClimate(params: { mode?: "off" | "heat" | "cool"; setpoint?: number }): Promise<ClimateStatus>`
  - `backend.getHotWaterStatus(): Promise<HotWaterStatus>`
  - `backend.setHotWater(params: { enable?: boolean; setpoint?: number }): Promise<HotWaterStatus>`
  - `backend.getOtStatus(): Promise<OtStatus>`
  - exported interfaces `ClimateStatus`, `HotWaterStatus`, `OtStatus`

- [ ] **Step 1: Add the three API methods**

Insert after the `reboot()` method (currently ~line 325), still inside the `class BackendService`:

```ts
  // ── Thermostat / OpenTherm ────────────────────────────────────

  async getClimateStatus(): Promise<ClimateStatus> {
    return this.send<ClimateStatus>("climateStatus")
  }

  /** mode and/or setpoint; reply is the full fresh climate status. */
  async setClimate(params: {
    mode?: "off" | "heat" | "cool"
    setpoint?: number
  }): Promise<ClimateStatus> {
    return this.send<ClimateStatus>("climateSet", params)
  }

  async getHotWaterStatus(): Promise<HotWaterStatus> {
    return this.send<HotWaterStatus>("hotWaterStatus")
  }

  /** enable and/or setpoint; `enable` maps to the device's int (1/0),
   *  omitted → the device leaves it unchanged. Reply is fresh DHW status. */
  async setHotWater(params: {
    enable?: boolean
    setpoint?: number
  }): Promise<HotWaterStatus> {
    const payload: Record<string, unknown> = {}
    if (params.enable !== undefined) payload.enable = params.enable ? 1 : 0
    if (params.setpoint !== undefined) payload.setpoint = params.setpoint
    return this.send<HotWaterStatus>("hotWaterSet", payload)
  }

  async getOtStatus(): Promise<OtStatus> {
    return this.send<OtStatus>("otStatus")
  }
```

- [ ] **Step 2: Add the three interfaces**

Append at the end of the file (after `PartitionsResponse`):

```ts
export interface ClimateStatus {
  mode: "off" | "heat" | "cool"
  userSetpoint: number
  activeSetpoint: number
  roomTemp: number
  roomValid: boolean
  pidOutput: number
  tSet: number
  chEnable: boolean
  coolEnable: boolean
  overrideActive: boolean
}

export interface HotWaterStatus {
  enable: boolean
  setpoint: number
  dhwActive: boolean
  dhwTemp: number
  dhwPresent: boolean
}

export interface OtStatus {
  linked: boolean
  fault: boolean
  chActive: boolean
  dhwActive: boolean
  flame: boolean
  coolingActive: boolean
  dhwPresent: boolean
  coolingSupported: boolean
  boilerTemp: number
  returnTemp: number
  dhwTemp: number
  modulation: number
  chPressure: number
  outsideTemp: number
  oemFaultCode: number
  oemDiagCode: number
  maxTSetUpper: number
  maxTSetLower: number
  overrideSetpoint: number
  chEnable: boolean
  dhwEnable: boolean
  coolEnable: boolean
  roomSetpoint: number
  dhwSetpoint: number
  tSet: number
}
```

- [ ] **Step 3: Typecheck**

Run: `cd frontend && pnpm typecheck`
Expected: PASS (no output / exit 0). The new methods reference the interfaces declared in the same module.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/lib/backend.ts
git commit -m "feat(web): backend wrappers for climate/DHW/OT status"
```

---

### Task 2: Thermostat page (hook + page + routing)

**Files:**
- Create: `frontend/src/hooks/use-thermostat-status.ts`
- Create: `frontend/src/pages/ThermostatPage.tsx`
- Modify: `frontend/src/components/AppSidebar.tsx` (add `ThermometerIcon` import + a `navItems` entry)
- Modify: `frontend/src/hooks/use-route.ts` (add `"thermostat"` to `validPages`)
- Modify: `frontend/src/App.tsx` (import `ThermostatPage`, add a `case`)

**Interfaces:**
- Consumes: `backend.getClimateStatus/setClimate`, `getHotWaterStatus/setHotWater`, `getOtStatus` (Task 1); `useConnectionStatus()`; `toast` from `sonner`.
- Produces: `useThermostatStatus()` returning `{ climate, dhw, ot, setMode, setSetpoint, setDhwEnable, setDhwSetpoint }`; default-exported `ThermostatPage`; a valid route/page `"thermostat"`.

- [ ] **Step 1: Create the data hook**

Create `frontend/src/hooks/use-thermostat-status.ts`:

```ts
import { useCallback, useEffect, useState } from "react"
import { toast } from "sonner"
import {
  backend,
  type ClimateStatus,
  type HotWaterStatus,
  type OtStatus,
} from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"

const POLL_MS = 2000

export function useThermostatStatus() {
  const connection = useConnectionStatus()
  const [climate, setClimate] = useState<ClimateStatus | null>(null)
  const [dhw, setDhw] = useState<HotWaterStatus | null>(null)
  const [ot, setOt] = useState<OtStatus | null>(null)

  // Polling is silent on error (a toast every 2 s on a flaky link is noise;
  // the connection-status indicator already signals the outage).
  const refresh = useCallback(() => {
    backend.getClimateStatus().then(setClimate).catch(() => {})
    backend.getHotWaterStatus().then(setDhw).catch(() => {})
    backend.getOtStatus().then(setOt).catch(() => {})
  }, [])

  useEffect(() => {
    if (connection !== "connected") return
    refresh()
    const interval = setInterval(refresh, POLL_MS)
    return () => clearInterval(interval)
  }, [connection, refresh])

  // Control actions DO toast on failure. Each reply is the fresh status.
  const setMode = useCallback((mode: "off" | "heat" | "cool") => {
    backend
      .setClimate({ mode })
      .then(setClimate)
      .catch((e: Error) => toast.error(`Mode change failed: ${e.message}`))
  }, [])

  const setSetpoint = useCallback((setpoint: number) => {
    backend
      .setClimate({ setpoint })
      .then(setClimate)
      .catch((e: Error) => toast.error(`Setpoint change failed: ${e.message}`))
  }, [])

  const setDhwEnable = useCallback((enable: boolean) => {
    backend
      .setHotWater({ enable })
      .then(setDhw)
      .catch((e: Error) => toast.error(`DHW change failed: ${e.message}`))
  }, [])

  const setDhwSetpoint = useCallback((setpoint: number) => {
    backend
      .setHotWater({ setpoint })
      .then(setDhw)
      .catch((e: Error) => toast.error(`DHW setpoint change failed: ${e.message}`))
  }, [])

  return { climate, dhw, ot, setMode, setSetpoint, setDhwEnable, setDhwSetpoint }
}
```

- [ ] **Step 2: Create the page**

Create `frontend/src/pages/ThermostatPage.tsx`. The setpoint controls keep a local "pending" value for snappy display and debounce the actual command by 500 ms; the pending value is cleared once a poll/reply confirms the device caught up.

```tsx
import { useEffect, useRef, useState } from "react"
import { ThermometerIcon } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Switch } from "@/components/ui/switch"
import { Badge } from "@/components/ui/badge"
import { useThermostatStatus } from "@/hooks/use-thermostat-status"

const SETPOINT_MIN = 5
const SETPOINT_MAX = 30
const SETPOINT_STEP = 0.5
const DHW_MIN = 30
const DHW_MAX = 80
const DHW_STEP = 1
const DEBOUNCE_MS = 500

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v))

export default function ThermostatPage() {
  const { climate, dhw, ot, setMode, setSetpoint, setDhwEnable, setDhwSetpoint } =
    useThermostatStatus()

  const [pendingSetpoint, setPendingSetpoint] = useState<number | null>(null)
  const [pendingDhw, setPendingDhw] = useState<number | null>(null)
  const spTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const dhwTimer = useRef<ReturnType<typeof setTimeout> | null>(null)

  // Drop the pending value once the device confirms it.
  useEffect(() => {
    if (
      pendingSetpoint !== null &&
      climate &&
      Math.abs(climate.userSetpoint - pendingSetpoint) < 0.01
    ) {
      setPendingSetpoint(null)
    }
  }, [climate, pendingSetpoint])
  useEffect(() => {
    if (pendingDhw !== null && dhw && Math.abs(dhw.setpoint - pendingDhw) < 0.01) {
      setPendingDhw(null)
    }
  }, [dhw, pendingDhw])

  if (!climate) {
    return (
      <div className="mx-auto max-w-2xl">
        <p className="text-sm text-muted-foreground">Connecting...</p>
      </div>
    )
  }

  const shownSetpoint = pendingSetpoint ?? climate.userSetpoint
  const nudgeSetpoint = (delta: number) => {
    const next = clamp(
      Math.round((shownSetpoint + delta) * 2) / 2,
      SETPOINT_MIN,
      SETPOINT_MAX,
    )
    setPendingSetpoint(next)
    if (spTimer.current) clearTimeout(spTimer.current)
    spTimer.current = setTimeout(() => setSetpoint(next), DEBOUNCE_MS)
  }

  const shownDhw = pendingDhw ?? dhw?.setpoint ?? DHW_MIN
  const nudgeDhw = (delta: number) => {
    const next = clamp(Math.round(shownDhw + delta), DHW_MIN, DHW_MAX)
    setPendingDhw(next)
    if (dhwTimer.current) clearTimeout(dhwTimer.current)
    dhwTimer.current = setTimeout(() => setDhwSetpoint(next), DEBOUNCE_MS)
  }

  const coolingSupported = ot?.coolingSupported ?? false
  const modes: Array<{ key: "off" | "heat" | "cool"; label: string }> = [
    { key: "off", label: "Off" },
    { key: "heat", label: "Heat" },
    ...(coolingSupported ? [{ key: "cool" as const, label: "Cool" }] : []),
  ]

  return (
    <div className="mx-auto max-w-2xl space-y-6">
      <h1 className="text-2xl font-bold">Thermostat</h1>

      <div className="rounded-xl border bg-card p-6 text-card-foreground shadow-sm">
        <div className="mb-4 flex items-center gap-2">
          <ThermometerIcon className="size-5 text-muted-foreground" />
          <h2 className="text-lg font-semibold">Room</h2>
        </div>
        <div className="flex items-center justify-between">
          <div className="text-5xl font-bold tabular-nums">
            {climate.roomValid ? `${climate.roomTemp.toFixed(1)}°` : "—"}
          </div>
          <div className="flex items-center gap-3">
            <Button
              variant="outline"
              size="icon"
              className="size-12 text-xl"
              onClick={() => nudgeSetpoint(-SETPOINT_STEP)}
            >
              −
            </Button>
            <div className="w-20 text-center">
              <div className="text-2xl font-semibold tabular-nums">
                {shownSetpoint.toFixed(1)}°
              </div>
              <div className="text-xs text-muted-foreground">Setpoint</div>
            </div>
            <Button
              variant="outline"
              size="icon"
              className="size-12 text-xl"
              onClick={() => nudgeSetpoint(SETPOINT_STEP)}
            >
              +
            </Button>
          </div>
        </div>
      </div>

      <div className="rounded-xl border bg-card p-6 text-card-foreground shadow-sm">
        <h2 className="mb-4 text-lg font-semibold">Mode</h2>
        <div className="flex gap-2">
          {modes.map((m) => (
            <Button
              key={m.key}
              variant={climate.mode === m.key ? "default" : "outline"}
              className="flex-1"
              onClick={() => setMode(m.key)}
            >
              {m.label}
            </Button>
          ))}
        </div>
      </div>

      <div className="rounded-xl border bg-card p-6 text-card-foreground shadow-sm">
        <div className="mb-4 flex items-center justify-between">
          <h2 className="text-lg font-semibold">Hot water</h2>
          <Switch
            checked={dhw?.enable ?? false}
            onCheckedChange={(v) => setDhwEnable(v)}
          />
        </div>
        <div className="flex items-center justify-between">
          <span className="text-sm text-muted-foreground">DHW setpoint</span>
          <div className="flex items-center gap-3">
            <Button variant="outline" size="icon" onClick={() => nudgeDhw(-DHW_STEP)}>
              −
            </Button>
            <span className="w-16 text-center text-lg font-semibold tabular-nums">
              {shownDhw.toFixed(0)}°
            </span>
            <Button variant="outline" size="icon" onClick={() => nudgeDhw(DHW_STEP)}>
              +
            </Button>
          </div>
        </div>
      </div>

      <div className="flex flex-wrap gap-2">
        <Badge variant="secondary">Active {climate.activeSetpoint.toFixed(1)}°</Badge>
        <Badge variant="secondary">t_set {climate.tSet.toFixed(0)}°</Badge>
        <Badge variant="secondary">PID {climate.pidOutput.toFixed(0)}</Badge>
        {ot?.flame && <Badge>Flame</Badge>}
        {ot?.chActive && <Badge>CH</Badge>}
        {ot?.dhwActive && <Badge>DHW</Badge>}
        {climate.overrideActive && <Badge variant="outline">Override</Badge>}
      </div>
    </div>
  )
}
```

- [ ] **Step 3: Wire the nav entry**

In `frontend/src/components/AppSidebar.tsx`, add `ThermometerIcon` to the lucide import (line 1) and a `navItems` entry after Home (line 20):

```ts
import {
  HomeIcon,
  TerminalIcon,
  SettingsIcon,
  DownloadIcon,
  ThermometerIcon,
} from "lucide-react"
```

```ts
const navItems = [
  { title: "Home", icon: HomeIcon, page: "home" as const },
  { title: "Thermostat", icon: ThermometerIcon, page: "thermostat" as const },
  { title: "Console", icon: TerminalIcon, page: "console" as const },
  { title: "Settings", icon: SettingsIcon, page: "settings" as const },
  { title: "Firmware", icon: DownloadIcon, page: "firmware" as const },
]
```

- [ ] **Step 4: Register the route**

In `frontend/src/hooks/use-route.ts`, add `"thermostat"` to `validPages`:

```ts
const validPages: Page[] = [
  "home",
  "thermostat",
  "console",
  "settings",
  "firmware",
]
```

- [ ] **Step 5: Add the render case**

In `frontend/src/App.tsx`, import the page (after the `HomePage` import) and add a `case` (after the `home` case):

```ts
import ThermostatPage from "@/pages/ThermostatPage"
```

```ts
    case "home":
      return <HomePage />
    case "thermostat":
      return <ThermostatPage />
```

- [ ] **Step 6: Typecheck**

Run: `cd frontend && pnpm typecheck`
Expected: PASS (exit 0).

- [ ] **Step 7: Commit**

```bash
git add frontend/src/hooks/use-thermostat-status.ts frontend/src/pages/ThermostatPage.tsx frontend/src/components/AppSidebar.tsx frontend/src/hooks/use-route.ts frontend/src/App.tsx
git commit -m "feat(web): thermostat control page"
```

---

### Task 3: Diagnostics page (hook + page + routing)

**Files:**
- Create: `frontend/src/hooks/use-diagnostics.ts`
- Create: `frontend/src/pages/DiagnosticsPage.tsx`
- Modify: `frontend/src/components/AppSidebar.tsx` (add `ActivityIcon` import + a `navItems` entry)
- Modify: `frontend/src/hooks/use-route.ts` (add `"diagnostics"` to `validPages`)
- Modify: `frontend/src/App.tsx` (import `DiagnosticsPage`, add a `case`)

**Interfaces:**
- Consumes: `backend.getOtStatus()` (Task 1); `useConnectionStatus()`.
- Produces: `useDiagnostics()` returning `OtStatus | null`; default-exported `DiagnosticsPage`; a valid route/page `"diagnostics"`.

- [ ] **Step 1: Create the data hook**

Create `frontend/src/hooks/use-diagnostics.ts`:

```ts
import { useEffect, useState } from "react"
import { backend, type OtStatus } from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"

const POLL_MS = 5000

export function useDiagnostics() {
  const connection = useConnectionStatus()
  const [ot, setOt] = useState<OtStatus | null>(null)

  useEffect(() => {
    if (connection !== "connected") return
    const fetchOt = () => backend.getOtStatus().then(setOt).catch(() => {})
    fetchOt()
    const interval = setInterval(fetchOt, POLL_MS)
    return () => clearInterval(interval)
  }, [connection])

  return ot
}
```

- [ ] **Step 2: Create the page**

Create `frontend/src/pages/DiagnosticsPage.tsx`. Values render `—` when the OT link is down.

```tsx
import type { ReactNode } from "react"
import { ActivityIcon } from "lucide-react"
import { Badge } from "@/components/ui/badge"
import { useDiagnostics } from "@/hooks/use-diagnostics"

export default function DiagnosticsPage() {
  const ot = useDiagnostics()

  if (!ot) {
    return (
      <div className="mx-auto max-w-2xl">
        <p className="text-sm text-muted-foreground">Connecting...</p>
      </div>
    )
  }

  const val = (n: number, unit: string, digits = 1) =>
    ot.linked ? `${n.toFixed(digits)}${unit}` : "—"

  return (
    <div className="mx-auto max-w-2xl space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold">Diagnostics</h1>
        <div className="flex items-center gap-2">
          <Badge variant={ot.linked ? "default" : "destructive"}>
            {ot.linked ? "Link up" : "Link down"}
          </Badge>
          {ot.fault && <Badge variant="destructive">Fault</Badge>}
          {ot.coolingSupported && <Badge variant="secondary">Cooling</Badge>}
        </div>
      </div>

      <Card title="Sensors">
        <Row label="Boiler temp" value={val(ot.boilerTemp, "°")} />
        <Row label="Return temp" value={val(ot.returnTemp, "°")} />
        <Row label="DHW temp" value={val(ot.dhwTemp, "°")} />
        <Row label="Modulation" value={val(ot.modulation, "%", 0)} />
        <Row label="CH pressure" value={val(ot.chPressure, " bar", 2)} />
        <Row label="Outside temp" value={val(ot.outsideTemp, "°")} />
      </Card>

      <Card title="Diagnostics">
        <Row label="OEM fault code" value={ot.linked ? String(ot.oemFaultCode) : "—"} />
        <Row label="OEM diag code" value={ot.linked ? String(ot.oemDiagCode) : "—"} />
      </Card>

      <Card title="Setpoint clamps">
        <Row label="t_set upper" value={`${ot.maxTSetUpper.toFixed(0)}°`} />
        <Row label="t_set lower" value={`${ot.maxTSetLower.toFixed(0)}°`} />
        <Row label="Current t_set" value={val(ot.tSet, "°", 0)} />
      </Card>
    </div>
  )
}

function Card({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="rounded-xl border bg-card p-6 text-card-foreground shadow-sm">
      <div className="mb-4 flex items-center gap-2">
        <ActivityIcon className="size-5 text-muted-foreground" />
        <h2 className="text-lg font-semibold">{title}</h2>
      </div>
      <div className="grid grid-cols-2 gap-x-8 gap-y-3 text-sm">{children}</div>
    </div>
  )
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between">
      <span className="text-muted-foreground">{label}</span>
      <span className="font-mono">{value}</span>
    </div>
  )
}
```

- [ ] **Step 3: Wire the nav entry**

In `frontend/src/components/AppSidebar.tsx`, add `ActivityIcon` to the lucide import and a `navItems` entry after Thermostat:

```ts
import {
  HomeIcon,
  TerminalIcon,
  SettingsIcon,
  DownloadIcon,
  ThermometerIcon,
  ActivityIcon,
} from "lucide-react"
```

```ts
  { title: "Thermostat", icon: ThermometerIcon, page: "thermostat" as const },
  { title: "Diagnostics", icon: ActivityIcon, page: "diagnostics" as const },
```

- [ ] **Step 4: Register the route**

In `frontend/src/hooks/use-route.ts`, add `"diagnostics"` to `validPages`:

```ts
const validPages: Page[] = [
  "home",
  "thermostat",
  "diagnostics",
  "console",
  "settings",
  "firmware",
]
```

- [ ] **Step 5: Add the render case**

In `frontend/src/App.tsx`, import the page and add a `case` after the `thermostat` case:

```ts
import DiagnosticsPage from "@/pages/DiagnosticsPage"
```

```ts
    case "thermostat":
      return <ThermostatPage />
    case "diagnostics":
      return <DiagnosticsPage />
```

- [ ] **Step 6: Typecheck**

Run: `cd frontend && pnpm typecheck`
Expected: PASS (exit 0).

- [ ] **Step 7: Commit**

```bash
git add frontend/src/hooks/use-diagnostics.ts frontend/src/pages/DiagnosticsPage.tsx frontend/src/components/AppSidebar.tsx frontend/src/hooks/use-route.ts frontend/src/App.tsx
git commit -m "feat(web): diagnostics page"
```

---

### Task 4: Build and on-device verification

**Files:** none (verification + gzip build output under `www/`).

**Interfaces:**
- Consumes: everything from Tasks 1–3.
- Produces: a built `www/` image and a verified running device.

- [ ] **Step 1: Full typecheck + production build**

Run: `cd frontend && pnpm typecheck && pnpm build`
Expected: `tsc -b` clean, `vite build` succeeds, gzipped assets written into `../www`.

- [ ] **Step 2: Flash and open the web UI**

Flash the device (per `CLAUDE.md`: `idf.py set-target esp32s3 && idf.py build && idf.py -p <PORT> flash`), connect to the device's WiFi/web UI, log in. Confirm **Thermostat** and **Diagnostics** appear in the sidebar and navigate to each.

- [ ] **Step 3: Drive the Thermostat page (device on the OT link to the gateway, THR=1)**

Verify, observing both the web UI and the gateway's view:
- Room temperature shows a live value (or `—` if the AHT20 reading is invalid).
- −/+ nudges the setpoint by 0.5°; a burst of clicks results in one write ~500 ms after the last click; the value settles to the device's clamped value; the gateway's room-setpoint (`CTMP`) reflects it.
- Mode Off/Heat/Cool switches; Cool only appears when the boiler advertises cooling support.
- DHW toggle flips the gateway's ID 0 DHW bit; DHW setpoint −/+ writes ID 56.
- Readout badges (Active/t_set/PID, Flame/CH/DHW, Override) track the live state.

- [ ] **Step 4: Drive the Diagnostics page**

Verify boiler sensor values, OEM codes, and clamps display; the link badge reads "Link up"; then pull the OT wire and confirm the badge flips to "Link down" and values show `—`, recovering when reconnected.

- [ ] **Step 5: Commit the build output**

```bash
git add www
git commit -m "build(web): rebuild www with thermostat + diagnostics pages"
```

---

## Notes / deferred (not in this plan)

- **Push/broadcast** via a future `MonitoringManager` — the per-page hooks are the swap seam; not built here.
- **Device-LCD command-driven retrofit / LVGL-in-WASM** — parked (`ui-command-driven.md`).
- **PID tuning UI** — already exposed via the generated Settings UI.
- **Generic command page** — separate backlog item (`command-page.md`).
