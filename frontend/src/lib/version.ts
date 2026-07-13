export function isPreRelease(version: string | undefined): boolean {
  if (!version) return false
  const parts = version.split(".")
  const patch = parts[2]
  return patch !== undefined && patch !== "0"
}
