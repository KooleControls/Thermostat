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
