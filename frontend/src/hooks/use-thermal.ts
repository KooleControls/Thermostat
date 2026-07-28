import { useCallback, useEffect, useState } from "react"
import { toast } from "sonner"
import { backend, type ThermalStatus, type ThermalModeName } from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"

const POLL_MS = 5000

/** Live state of the self-heating test rig, plus the setters for its levers.
 *  Polling is slow on purpose: the thing being watched moves over tens of
 *  minutes, and the device is often deliberately running at 80 MHz. */
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

  // Every setter's reply is the fresh status, so the UI never has to guess what
  // the device did with the request.
  const apply = useCallback(
    (params: Parameters<typeof backend.setThermal>[0], what: string) => {
      backend
        .setThermal(params)
        .then(setStatus)
        .catch((e: Error) => toast.error(`${what} failed: ${e.message}`))
    },
    [],
  )

  const setMode = useCallback(
    (mode: ThermalModeName) => apply({ mode }, "Mode change"),
    [apply],
  )
  const setCycle = useCallback(
    (cycle: boolean) => apply({ cycle }, "Cycle change"),
    [apply],
  )
  const setCyclePair = useCallback(
    (cycleA: ThermalModeName, cycleB: ThermalModeName) =>
      apply({ cycleA, cycleB }, "Cycle change"),
    [apply],
  )
  const setDwell = useCallback(
    (dwellMin: number) => apply({ dwellMin }, "Dwell change"),
    [apply],
  )
  const setSampleSec = useCallback(
    (sampleSec: number) => apply({ sampleSec }, "Sample interval change"),
    [apply],
  )
  const setBacklight = useCallback(
    (backlight: number) => apply({ backlight }, "Brightness change"),
    [apply],
  )

  return {
    status,
    refresh,
    setMode,
    setCycle,
    setCyclePair,
    setDwell,
    setSampleSec,
    setBacklight,
  }
}
