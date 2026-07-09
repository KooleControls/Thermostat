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
