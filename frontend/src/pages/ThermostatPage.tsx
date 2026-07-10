import { useEffect, useRef, useState } from "react"
import { ThermometerIcon } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Switch } from "@/components/ui/switch"
import { Badge } from "@/components/ui/badge"
import { StateBadge } from "@/components/StateBadge"
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
        <StateBadge label="Link" on={!!ot?.linked} onLabel="Up" offLabel="Down" offVariant="destructive" />
        <StateBadge label="Flame" on={!!ot?.flame} />
        <StateBadge label="CH" on={!!ot?.chActive} />
        <StateBadge label="DHW" on={!!ot?.dhwActive} />
        <StateBadge label="Override" on={climate.overrideActive} />
      </div>
    </div>
  )
}
