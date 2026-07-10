import type { ReactNode } from "react"
import { ActivityIcon } from "lucide-react"
import { StateBadge } from "@/components/StateBadge"
import { useDiagnostics } from "@/hooks/use-diagnostics"

export default function DiagnosticsPage() {
  const ot = useDiagnostics()

  // Header (esp. Link) always renders — never hidden behind a "Connecting…"
  // guard — so link state is always visible. Sensor values show "—" until
  // there's a live, linked reading.
  const linked = ot?.linked ?? false
  const val = (n: number | undefined, unit: string, digits = 1) =>
    linked && n !== undefined ? `${n.toFixed(digits)}${unit}` : "—"

  return (
    <div className="mx-auto max-w-2xl space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold">Diagnostics</h1>
        <div className="flex items-center gap-2">
          <StateBadge label="Link" on={linked} onLabel="Up" offLabel="Down" offVariant="destructive" />
          <StateBadge label="Fault" on={ot?.fault ?? false} onVariant="destructive" />
          <StateBadge label="Cooling" on={ot?.coolingSupported ?? false} />
        </div>
      </div>

      <Card title="Sensors">
        <Row label="Boiler temp" value={val(ot?.boilerTemp, "°")} />
        <Row label="Return temp" value={val(ot?.returnTemp, "°")} />
        <Row label="DHW temp" value={val(ot?.dhwTemp, "°")} />
        <Row label="Modulation" value={val(ot?.modulation, "%", 0)} />
        <Row label="CH pressure" value={val(ot?.chPressure, " bar", 2)} />
        <Row label="Outside temp" value={val(ot?.outsideTemp, "°")} />
      </Card>

      <Card title="Diagnostics">
        <Row label="OEM fault code" value={linked ? String(ot?.oemFaultCode) : "—"} />
        <Row label="OEM diag code" value={linked ? String(ot?.oemDiagCode) : "—"} />
      </Card>

      <Card title="Setpoint clamps">
        <Row label="t_set upper" value={ot ? `${ot.maxTSetUpper.toFixed(0)}°` : "—"} />
        <Row label="t_set lower" value={ot ? `${ot.maxTSetLower.toFixed(0)}°` : "—"} />
        <Row label="Current t_set" value={val(ot?.tSet, "°", 0)} />
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
