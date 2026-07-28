import type { ReactNode } from "react"
import { FlameIcon } from "lucide-react"
import { Button } from "@/components/ui/button"
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
import type { ThermalModeName } from "@/lib/backend"

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

const BRIGHTNESS_STEPS = [0, 25, 50, 100]

const duration = (seconds: number) => {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  return h > 0 ? `${h} h ${m} m` : `${m} m`
}

export default function ThermalPage() {
  const { status, setMode, setBacklight, setCpuMhz, stopRadio } = useThermal()

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

        <div className="col-span-2 mt-4 flex items-center justify-between">
          <div className="pr-4">
            <div className="font-medium text-foreground">WiFi radio</div>
            <div className="text-xs text-muted-foreground">
              The receiver is awake continuously (power save is off for latency),
              so it draws the whole time. Stopping it is reboot-only: with the
              radio down there is nothing left to ask.
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
          For finding the dimmest setting that is still readable on a wall.
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

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between">
      <span className="text-muted-foreground">{label}</span>
      <span className="font-mono">{value}</span>
    </div>
  )
}
