import { useEffect, useState, type ReactNode } from "react"
import { FlameIcon } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Badge } from "@/components/ui/badge"
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog"
import { StateBadge } from "@/components/StateBadge"
import { useThermal } from "@/hooks/use-thermal"
import type { ThermalModeName, WifiPowerSave } from "@/lib/backend"

// Power states, coarsest lever first. Everything here keeps OpenTherm alive, so
// the gateway keeps logging room temperature throughout — that log is the
// measurement, not this page. "custom" is what the device reports when only the
// brightness was set.
const MODES: Array<{ key: ThermalModeName; label: string; detail: string }> = [
  { key: "baseline", label: "Baseline", detail: "As shipped — backlight 100 %, full refresh, 240 MHz" },
  { key: "dark", label: "Dark screen", detail: "Backlight fully off" },
  { key: "panelidle", label: "Panel idle", detail: "Backlight off, refresh clock slowed to a crawl" },
  { key: "paneloff", label: "Panel off", detail: "Backlight off, panel held in reset, CPU at 80 MHz" },
]

// Modem sleep depth. "none" is the shipped default (lowest latency, receiver
// always awake); the other two park it between beacons and keep the association.
const PS_MODES: Array<{ key: WifiPowerSave; label: string }> = [
  { key: "none", label: "No sleep" },
  { key: "min", label: "Min modem" },
  { key: "max", label: "Max modem" },
]

const duration = (seconds: number) => {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  return h > 0 ? `${h} h ${m} m` : `${m} m`
}

export default function ThermalPage() {
  const { status, setMode, setBacklight, setCpuMhz, setWifiPs, stopRadio } =
    useThermal()

  if (!status) {
    return (
      <div className="mx-auto max-w-2xl">
        <p className="text-sm text-muted-foreground">Connecting...</p>
      </div>
    )
  }

  const active = MODES.find((m) => m.key === status.mode)

  return (
    <div className="mx-auto max-w-2xl space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold">Self-heating</h1>
        <div className="flex items-center gap-2">
          {status.panelDead && <Badge variant="destructive">Panel dead</Badge>}
          {status.radioStopped && <Badge variant="destructive">WiFi off</Badge>}
          <StateBadge
            label="OT"
            on={status.otLinked}
            onLabel="Up"
            offLabel="Down"
            offVariant="destructive"
          />
        </div>
      </div>

      <Card title="Power state">
        <div className="col-span-2 grid grid-cols-2 gap-2">
          {MODES.map((m) =>
            // Holding the panel in reset cannot be undone without a reboot, so
            // that one asks first.
            m.key === "paneloff" ? (
              <AlertDialog key={m.key}>
                <AlertDialogTrigger asChild>
                  <Button
                    variant={status.mode === m.key ? "default" : "outline"}
                    className="h-12"
                  >
                    {m.label}
                  </Button>
                </AlertDialogTrigger>
                <AlertDialogContent>
                  <AlertDialogHeader>
                    <AlertDialogTitle>Hold the panel in reset?</AlertDialogTitle>
                    <AlertDialogDescription>
                      This is one-way: only a reboot brings the display back,
                      because the panel's command pins are the OpenTherm UART
                      now. WiFi and OpenTherm keep running, so the gateway goes
                      on logging.
                    </AlertDialogDescription>
                  </AlertDialogHeader>
                  <AlertDialogFooter>
                    <AlertDialogCancel>Cancel</AlertDialogCancel>
                    <AlertDialogAction onClick={() => setMode(m.key)}>
                      Turn the panel off
                    </AlertDialogAction>
                  </AlertDialogFooter>
                </AlertDialogContent>
              </AlertDialog>
            ) : (
              <Button
                key={m.key}
                variant={status.mode === m.key ? "default" : "outline"}
                className="h-12"
                onClick={() => setMode(m.key)}
              >
                {m.label}
              </Button>
            ),
          )}
        </div>

        <p className="col-span-2 mt-4 text-sm text-muted-foreground">
          {status.mode === "custom" ? "Brightness set by hand." : (active?.detail ?? "")}
        </p>

        <div className="col-span-2 mt-3 flex flex-wrap gap-2">
          <Badge variant="secondary">Backlight {status.backlight}%</Badge>
          <Badge variant="secondary">pclk {(status.pclkHz / 1_000_000).toFixed(1)} MHz</Badge>
          <Badge variant="secondary">
            CPU {status.cpuMhz} MHz{status.cpuControl ? "" : " (fixed)"}
          </Badge>
          <Badge variant="secondary">
            WiFi {status.radioStopped ? "off" : `PS ${status.wifiPs}`}
          </Badge>
          <Badge variant="secondary">In state {duration(status.secondsInState)}</Badge>
        </div>
      </Card>

      <Card title="Now">
        <Row label="Room sensor" value={status.roomValid ? `${status.room.toFixed(2)} °C` : "—"} />
        <Row
          label="Humidity"
          value={status.humidityValid ? `${status.humidity.toFixed(1)} %` : "—"}
        />
        <Row label="Die temperature" value={status.dieValid ? `${status.die.toFixed(1)} °C` : "—"} />
        <p className="col-span-2 mt-1 text-xs text-muted-foreground">
          The room figure is what goes out over OpenTherm, so the gateway's log
          is the record. Die temperature is shown here and used for nothing else.
        </p>
      </Card>

      <Card title="ESP itself">
        <p className="col-span-2 text-sm text-muted-foreground">
          The SoC contributes too, so these move on their own rather than only as
          part of the ladder above. Both keep OpenTherm running, so the gateway
          keeps logging either way.
        </p>

        <div className="col-span-2 mt-3">
          <span className="text-xs text-muted-foreground">CPU clock</span>
          <div className="mt-1 flex gap-2">
            {([240, 160, 80] as const).map((mhz) => (
              <Button
                key={mhz}
                variant={status.cpuMhz === mhz ? "default" : "outline"}
                className="flex-1"
                disabled={!status.cpuControl}
                onClick={() => setCpuMhz(mhz)}
              >
                {mhz} MHz
              </Button>
            ))}
          </div>
        </div>

        <div className="col-span-2 mt-4">
          <span className="text-xs text-muted-foreground">
            WiFi power save — all three keep the connection, so these are
            reversible; deeper sleep costs round-trip latency
          </span>
          <div className="mt-1 flex gap-2">
            {PS_MODES.map((ps) => (
              <Button
                key={ps.key}
                variant={status.wifiPs === ps.key ? "default" : "outline"}
                className="flex-1"
                disabled={status.radioStopped}
                onClick={() => setWifiPs(ps.key)}
              >
                {ps.label}
              </Button>
            ))}
          </div>
        </div>

        <div className="col-span-2 mt-4 flex items-center justify-between">
          <div className="pr-4">
            <div className="font-medium text-foreground">Stop the radio entirely</div>
            <div className="text-xs text-muted-foreground">
              The floor for the radio, and the only one that is not reversible:
              with WiFi down there is nothing left to ask, so it takes a reboot.
              OpenTherm and the display keep working.
            </div>
          </div>
          {status.radioStopped ? (
            <Badge variant="destructive">Stopped</Badge>
          ) : (
            <AlertDialog>
              <AlertDialogTrigger asChild>
                <Button
                  variant="outline"
                  className="shrink-0 text-destructive hover:text-destructive"
                >
                  Stop WiFi
                </Button>
              </AlertDialogTrigger>
              <AlertDialogContent>
                <AlertDialogHeader>
                  <AlertDialogTitle>Stop the WiFi radio?</AlertDialogTitle>
                  <AlertDialogDescription>
                    You lose this page until the unit is rebooted. OpenTherm and
                    the display keep working, so the gateway goes on logging room
                    temperature throughout.
                  </AlertDialogDescription>
                </AlertDialogHeader>
                <AlertDialogFooter>
                  <AlertDialogCancel>Cancel</AlertDialogCancel>
                  <AlertDialogAction onClick={stopRadio}>Stop WiFi</AlertDialogAction>
                </AlertDialogFooter>
              </AlertDialogContent>
            </AlertDialog>
          )}
        </div>
      </Card>

      <Card title="Brightness">
        <div className="col-span-2">
          <BrightnessInput value={status.backlight} onCommit={setBacklight} />
        </div>
        <p className="col-span-2 mt-3 text-xs text-muted-foreground">
          Any duty from 0 to 100 %. Kept across a reboot, so a long run does not
          silently return to full brightness — the console says which value was
          restored.
        </p>
      </Card>
    </div>
  )
}

// Free entry rather than fixed steps: the test needs whatever duty the last run
// suggested (30 %, then wherever the knee turns out to be), not a menu. Committed
// on Enter or the button — every commit is a device round trip that writes NVS.
function BrightnessInput({
  value,
  onCommit,
}: {
  value: number
  onCommit: (v: number) => void
}) {
  const [draft, setDraft] = useState(String(value))

  // Follow the device unless the field is mid-edit, so polling can't overwrite
  // what is being typed.
  const [editing, setEditing] = useState(false)
  useEffect(() => {
    if (!editing) setDraft(String(value))
  }, [value, editing])

  const commit = () => {
    setEditing(false)
    const parsed = Number(draft)
    if (!Number.isFinite(parsed)) {
      setDraft(String(value))
      return
    }
    const clamped = Math.min(100, Math.max(0, Math.round(parsed)))
    setDraft(String(clamped))
    if (clamped !== value) onCommit(clamped)
  }

  return (
    <div className="flex items-end gap-3">
      <label className="block">
        <span className="text-xs text-muted-foreground">Duty (%)</span>
        <Input
          className="mt-1 h-10 w-28 font-mono"
          inputMode="numeric"
          value={draft}
          onChange={(e) => {
            setEditing(true)
            setDraft(e.target.value)
          }}
          onBlur={commit}
          onKeyDown={(e) => {
            if (e.key === "Enter") commit()
          }}
        />
      </label>
      <Button className="h-10" onClick={commit}>
        Apply
      </Button>
      <span className="pb-2 text-sm text-muted-foreground">
        now {value}%
      </span>
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

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between">
      <span className="text-muted-foreground">{label}</span>
      <span className="font-mono">{value}</span>
    </div>
  )
}
