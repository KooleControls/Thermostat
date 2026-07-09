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
