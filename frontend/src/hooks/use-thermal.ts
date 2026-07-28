import { useCallback, useEffect, useState } from "react"
import { toast } from "sonner"
import { backend, type ThermalStatus, type ThermalModeName } from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"

const POLL_MS = 5000

/** Live power state, plus the two setters that change it. Polling is slow on
 *  purpose: the thing being watched moves over tens of minutes, and the device
 *  may be running at 80 MHz. The measurement itself lives in the gateway's log,
 *  not here. */
export function useThermal() {
  const connection = useConnectionStatus()
  const [status, setStatus] = useState<ThermalStatus | null>(null)

  const refresh = useCallback(() => {
    backend.getThermalStatus().then(setStatus).catch(() => {})
  }, [])

  useEffect(() => {
    if (connection !== "connected") return
    refresh()
    const interval = setInterval(refresh, POLL_MS)
    return () => clearInterval(interval)
  }, [connection, refresh])

  // Each reply is the fresh status, so the UI never guesses what the device did.
  const setMode = useCallback((mode: ThermalModeName) => {
    backend
      .setThermal({ mode })
      .then(setStatus)
      .catch((e: Error) => toast.error(`Mode change failed: ${e.message}`))
  }, [])

  const setBacklight = useCallback((backlight: number) => {
    backend
      .setThermal({ backlight })
      .then(setStatus)
      .catch((e: Error) => toast.error(`Brightness change failed: ${e.message}`))
  }, [])

  return { status, refresh, setMode, setBacklight }
}
