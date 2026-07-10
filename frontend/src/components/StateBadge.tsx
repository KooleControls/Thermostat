import { Badge } from "@/components/ui/badge"

type BadgeVariant = "default" | "secondary" | "destructive" | "outline"

// Always-visible state indicator: renders `Label: value` for both states so a
// field never disappears — you can always see what is (or should be) available.
export function StateBadge({
  label,
  on,
  onLabel = "Yes",
  offLabel = "No",
  onVariant = "default",
  offVariant = "outline",
}: {
  label: string
  on: boolean
  onLabel?: string
  offLabel?: string
  onVariant?: BadgeVariant
  offVariant?: BadgeVariant
}) {
  return (
    <Badge variant={on ? onVariant : offVariant}>
      {label}: {on ? onLabel : offLabel}
    </Badge>
  )
}
