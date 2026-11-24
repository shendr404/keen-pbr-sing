import { apiFetch } from "@/api/client"

export type SingboxConnection = {
  id: string
  name: string
  protocol: string
  server: string
  port: number
  source: string
}

export type SingboxProfile = {
  id: string
  name: string
  interface: string
  keenetic_tag: string
  mode: "direct" | "selector" | "urltest" | string
  active: string
  has_subscription: boolean
  interval_hours: number
  last_refresh: number
  next_refresh?: number | null
  last_error: string
  connections: SingboxConnection[]
}

export type SingboxState = {
  profiles: SingboxProfile[]
  warning?: string
  singbox_checked?: boolean
  singbox_reloaded?: boolean
  keenetic_applied?: boolean
}

type Envelope = { data: SingboxState }

export const fetchSingboxProfiles = async () => {
  const response = await apiFetch<Envelope>("/api/sing-box/profiles", { method: "GET" })
  return response.data
}

export const postSingbox = async (path: string, body: unknown) => {
  const response = await apiFetch<Envelope>(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  })
  return response.data
}

export const previewSingboxLink = async (link: string) => {
  const response = await apiFetch<{
    data: { protocol: string; name: string; server: string; port: number }
  }>("/api/sing-box/preview", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ link }),
  })
  return response.data
}
