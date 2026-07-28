import { useEffect, useState, type ReactNode } from "react"
import { FlameIcon } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Switch } from "@/components/ui/switch"
import { Badge } from "@/components/ui/badge"
import { StateBadge } from "@/components/StateBadge"
import { useThermal } from "@/hooks/use-thermal"
import type { ThermalModeName } from "@/lib/backend"

// The four defined states of the rig, coarsest lever first. "custom" is not
// offered as a button — it is what the device reports when a single lever was
// set directly (over the command surface or the brightness row below).
const MODES: Array<{ key: ThermalModeName; label: string; detail: string }> = [
  { key: "baseline", label: "Baseline", detail: "As shipped — backlight 100 %, full refresh, 240 MHz" },
  { key: "dark", label: "Dark screen", detail: "Backlight fully off" },
  { key: "panelidle", label: "Panel idle", detail: "Backlight off, refresh clock slowed to a crawl" },
  { key: "lowpower", label: "Low power", detail: "Backlight off, slow refresh, CPU at 80 MHz" },
]

const MODE_LABEL: Record<ThermalModeName, string> = {
  baseline: "Baseline",
  dark: "Dark screen",
  panelidle: "Panel idle",
  lowpower: "Low power",
  custom: "Custom",
}

const BRIGHTNESS_STEPS = [0, 25, 50, 100]

const duration = (seconds: number) => {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  return h > 0 ? `${h} h ${m} m` : `${m} m`
}

const signed = (v: number, digits = 2) => `${v > 0 ? "+" : ""}${v.toFixed(digits)}`

export default function ThermalPage() {
  const { status, setMode, setCycle, setCyclePair, setDwell, setSampleSec, setBacklight } =
    useThermal()

  if (!status) {
    return (
      <div className="mx-auto max-w-2xl">
        <p className="text-sm text-muted-foreground">Connecting...</p>
      </div>
    )
  }

  const active = MODES.find((m) => m.key === status.mode)
  const windowLabel = `${Math.round(status.deltaWindowS / 60)} min`

  return (
    <div className="mx-auto max-w-2xl space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold">Self-heating test</h1>
        <div className="flex items-center gap-2">
          <StateBadge label="Cycle" on={status.cycle} />
          <StateBadge
            label="OT"
            on={status.otLinked}
            onLabel="Up"
            offLabel="Down"
            offVariant="destructive"
          />
        </div>
      </div>

      <Card title="Mode">
        <div className="grid grid-cols-2 gap-2">
          {MODES.map((m) => (
            <Button
              key={m.key}
              variant={status.mode === m.key ? "default" : "outline"}
              className="h-12"
              onClick={() => setMode(m.key)}
            >
              {m.label}
            </Button>
          ))}
        </div>
        <p className="mt-4 text-sm text-muted-foreground">
          {status.mode === "custom"
            ? "Custom — levers set individually."
            : (active?.detail ?? "")}
        </p>
        <div className="mt-3 flex flex-wrap gap-2">
          <Badge variant="secondary">Backlight {status.backlight}%</Badge>
          <Badge variant="secondary">
            pclk {(status.pclkHz / 1_000_000).toFixed(1)} MHz
          </Badge>
          <Badge variant="secondary">
            CPU {status.cpuMhz} MHz{status.cpuControl ? "" : " (fixed)"}
          </Badge>
          <Badge variant="secondary">In state {duration(status.secondsInState)}</Badge>
        </div>
        <p className="mt-3 text-xs text-muted-foreground">
          The panel stays dark until you change the mode here — a touch
          deliberately does not wake it, so a running measurement can't be
          disturbed by a passing hand.
        </p>
      </Card>

      <Card title={`Now (change over the last ${windowLabel})`}>
        <Row
          label="Room sensor"
          value={status.roomValid ? `${status.room.toFixed(2)} °C` : "—"}
          delta={status.roomDelta}
        />
        <Row
          label="Humidity"
          value={status.humidityValid ? `${status.humidity.toFixed(1)} %` : "—"}
        />
        <Row
          label="Die temperature"
          value={status.dieValid ? `${status.die.toFixed(1)} °C` : "—"}
          delta={status.dieDelta}
        />
        <Row label="Samples held" value={`${status.samples}`} />
        <p className="col-span-2 mt-1 text-xs text-muted-foreground">
          Every sample is also one CSV line on the Console page, prefixed
          THERMAL — that, or tools/thermal_log.py, is the record to keep for a
          long run.
        </p>
      </Card>

      <Card title="A/B cycle">
        <div className="col-span-2 flex items-center justify-between">
          <div>
            <div className="font-medium text-foreground">Alternate two modes</div>
            <div className="text-xs text-muted-foreground">
              Ambient drift over an afternoon is bigger than the effect being
              measured, so a single before/after run proves nothing. Cycling
              lets the analysis compare like with like.
            </div>
          </div>
          <Switch checked={status.cycle} onCheckedChange={setCycle} />
        </div>

        <div className="col-span-2 mt-4 grid grid-cols-2 gap-4">
          <ModePicker
            label="State A"
            value={status.cycleA}
            onChange={(v) => setCyclePair(v, status.cycleB)}
          />
          <ModePicker
            label="State B"
            value={status.cycleB}
            onChange={(v) => setCyclePair(status.cycleA, v)}
          />
        </div>

        <div className="col-span-2 mt-4 grid grid-cols-2 gap-4">
          <NumberField
            label="Dwell (minutes)"
            hint="≥ 60 keeps only settled data"
            value={status.dwellMin}
            min={1}
            max={720}
            onCommit={setDwell}
          />
          <NumberField
            label="Sample interval (s)"
            value={status.sampleSec}
            min={5}
            max={600}
            onCommit={setSampleSec}
          />
        </div>
      </Card>

      <Card title="Brightness">
        <div className="col-span-2 flex flex-wrap gap-2">
          {BRIGHTNESS_STEPS.map((p) => (
            <Button
              key={p}
              variant={status.backlight === p ? "default" : "outline"}
              className="flex-1"
              onClick={() => setBacklight(p)}
            >
              {p}%
            </Button>
          ))}
        </div>
        <p className="col-span-2 mt-3 text-xs text-muted-foreground">
          Sets the backlight on its own, which puts the rig in Custom — useful
          for finding the dimmest setting that is still readable on a wall.
        </p>
      </Card>
    </div>
  )
}

function Card({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="rounded-xl border bg-card p-6 text-card-foreground shadow-sm">
      <div className="mb-4 flex items-center gap-2">
        <FlameIcon className="size-5 text-muted-foreground" />
        <h2 className="text-lg font-semibold">{title}</h2>
      </div>
      <div className="grid grid-cols-2 gap-x-8 gap-y-3 text-sm">{children}</div>
    </div>
  )
}

function Row({
  label,
  value,
  delta,
}: {
  label: string
  value: string
  delta?: number
}) {
  return (
    <div className="flex justify-between">
      <span className="text-muted-foreground">{label}</span>
      <span className="font-mono">
        {value}
        {delta !== undefined && (
          <span className="ml-2 text-muted-foreground">{signed(delta)}</span>
        )}
      </span>
    </div>
  )
}

function ModePicker({
  label,
  value,
  onChange,
}: {
  label: string
  value: ThermalModeName
  onChange: (v: ThermalModeName) => void
}) {
  return (
    <label className="block">
      <span className="text-xs text-muted-foreground">{label}</span>
      <select
        className="mt-1 h-9 w-full rounded-md border bg-background px-2 text-sm"
        value={value}
        onChange={(e) => onChange(e.target.value as ThermalModeName)}
      >
        {MODES.map((m) => (
          <option key={m.key} value={m.key}>
            {MODE_LABEL[m.key]}
          </option>
        ))}
      </select>
    </label>
  )
}

// Committed on blur or Enter, not on every keystroke: each commit is a device
// round trip that also writes NVS.
function NumberField({
  label,
  hint,
  value,
  min,
  max,
  onCommit,
}: {
  label: string
  hint?: string
  value: number
  min: number
  max: number
  onCommit: (v: number) => void
}) {
  const [draft, setDraft] = useState(String(value))
  useEffect(() => setDraft(String(value)), [value])

  const commit = () => {
    const parsed = Number(draft)
    if (!Number.isFinite(parsed)) {
      setDraft(String(value))
      return
    }
    const clamped = Math.min(max, Math.max(min, Math.round(parsed)))
    setDraft(String(clamped))
    if (clamped !== value) onCommit(clamped)
  }

  return (
    <label className="block">
      <span className="text-xs text-muted-foreground">{label}</span>
      <Input
        className="mt-1 h-9"
        inputMode="numeric"
        value={draft}
        onChange={(e) => setDraft(e.target.value)}
        onBlur={commit}
        onKeyDown={(e) => {
          if (e.key === "Enter") commit()
        }}
      />
      {hint && <span className="text-xs text-muted-foreground">{hint}</span>}
    </label>
  )
}
