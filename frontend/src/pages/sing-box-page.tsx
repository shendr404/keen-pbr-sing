import { useState } from "react"
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query"
import { useTranslation } from "react-i18next"

import {
  fetchSingboxProfiles,
  postSingbox,
  previewSingboxLink,
  type SingboxProfile,
} from "@/api/sing-box"
import { PageHeader } from "@/components/shared/page-header"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import type { ApiError } from "@/api/client"
import { getApiErrorMessage } from "@/lib/api-errors"

const queryKey = ["sing-box-profiles"]

export function SingboxPage() {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const profiles = useQuery({ queryKey, queryFn: fetchSingboxProfiles })
  const [link, setLink] = useState("")
  const [name, setName] = useState("")
  const [tag, setTag] = useState("proxy")
  const [iface, setIface] = useState("sbtun")
  const [mode, setMode] = useState("selector")
  const [preview, setPreview] = useState<string>("")
  const [subscriptionUrl, setSubscriptionUrl] = useState("")
  const [intervalHours, setIntervalHours] = useState("0")
  const [message, setMessage] = useState("")

  const invalidate = () => queryClient.invalidateQueries({ queryKey })
  const fail = (error: unknown) =>
    setMessage(
      error && typeof error === "object" && "message" in error
        ? getApiErrorMessage(error as ApiError)
        : t("pages.singbox.failed")
    )

  const save = useMutation({
    mutationFn: () =>
      postSingbox("/api/sing-box/connections", {
        profile: tag,
        interface: iface,
        tag,
        mode,
        name,
        link,
      }),
    onSuccess: async (state) => {
      setMessage(state.warning ?? t("pages.singbox.saved"))
      setLink("")
      setPreview("")
      await invalidate()
    },
    onError: fail,
  })
  const subscribe = useMutation({
    mutationFn: () =>
      postSingbox("/api/sing-box/subscriptions", {
        profile: tag,
        interface: iface,
        tag,
        mode: mode === "selector" ? "urltest" : mode,
        url: subscriptionUrl,
        interval_hours: Number(intervalHours) || 0,
      }),
    onSuccess: async (state) => {
      setMessage(state.warning ?? t("pages.singbox.subscriptionSaved"))
      await invalidate()
    },
    onError: fail,
  })

  const runPreview = async () => {
    setMessage("")
    try {
      const parsed = await previewSingboxLink(link)
      setName(parsed.name)
      setPreview(`${parsed.protocol} ${parsed.server}:${parsed.port}`)
    } catch (error) {
      setPreview("")
      fail(error)
    }
  }

  return (
    <div className="space-y-6">
      <PageHeader description={t("pages.singbox.description")} title={t("pages.singbox.title")} />
      <section className="space-y-3 rounded-lg border p-4">
        <h2 className="text-base font-medium">{t("pages.singbox.addLink")}</h2>
        <textarea
          className="min-h-20 w-full rounded-lg border bg-transparent p-2 text-sm"
          onChange={(event) => setLink(event.target.value)}
          placeholder="vless://…  hysteria2://…  trojan://…  ss://…  vmess://…"
          value={link}
        />
        <div className="flex flex-wrap gap-2">
          <Button onClick={runPreview} type="button" variant="outline">
            {t("pages.singbox.preview")}
          </Button>
          <Button disabled={save.isPending || link.length === 0} onClick={() => save.mutate()} type="button">
            {t("pages.singbox.save")}
          </Button>
        </div>
        {preview ? <p className="text-sm text-muted-foreground">{preview}</p> : null}
        <div className="grid gap-2 md:grid-cols-3">
          <Input onChange={(event) => setName(event.target.value)} placeholder={t("pages.singbox.name")} value={name} />
          <Input onChange={(event) => setTag(event.target.value)} placeholder={t("pages.singbox.tag")} value={tag} />
          <Input onChange={(event) => setIface(event.target.value)} placeholder={t("pages.singbox.interface")} value={iface} />
        </div>
        <label className="block text-sm">
          {t("pages.singbox.mode")}
          <select className="mt-1 block h-8 rounded-lg border bg-transparent px-2" onChange={(event) => setMode(event.target.value)} value={mode}>
            <option value="direct">direct</option>
            <option value="selector">selector</option>
            <option value="urltest">urltest</option>
          </select>
        </label>
      </section>
      <section className="space-y-3 rounded-lg border p-4">
        <h2 className="text-base font-medium">{t("pages.singbox.subscription")}</h2>
        <Input onChange={(event) => setSubscriptionUrl(event.target.value)} placeholder="https://…" value={subscriptionUrl} />
        <Input onChange={(event) => setIntervalHours(event.target.value)} placeholder={t("pages.singbox.interval")} value={intervalHours} />
        <Button disabled={subscribe.isPending || subscriptionUrl.length === 0} onClick={() => subscribe.mutate()} type="button">
          {t("pages.singbox.fetchSubscription")}
        </Button>
      </section>
      {message ? <p className="text-sm">{message}</p> : null}
      {profiles.data?.profiles.map((profile) => (
        <ProfileCard
          key={profile.id}
          onChange={async () => {
            setMessage("")
            await invalidate()
          }}
          onError={fail}
          profile={profile}
        />
      ))}
    </div>
  )
}

function ProfileCard({
  profile,
  onChange,
  onError,
}: {
  profile: SingboxProfile
  onChange: () => Promise<void>
  onError: (error: unknown) => void
}) {
  const { t } = useTranslation()
  const select = useMutation({
    mutationFn: (body: { mode?: string; active?: string }) =>
      postSingbox("/api/sing-box/profiles/select", { profile_id: profile.id, mode: body.mode ?? profile.mode, active: body.active ?? profile.active }),
    onSuccess: onChange,
    onError,
  })
  const remove = useMutation({
    mutationFn: (id: string) => postSingbox("/api/sing-box/connections/delete", { profile_id: profile.id, id }),
    onSuccess: onChange,
    onError,
  })
  const refresh = useMutation({
    mutationFn: () => postSingbox("/api/sing-box/subscriptions/refresh", { profile_id: profile.id }),
    onSuccess: onChange,
    onError,
  })
  return (
    <section className="space-y-3 rounded-lg border p-4">
      <div className="flex flex-wrap items-baseline justify-between gap-2">
        <h2 className="text-base font-medium">{profile.name}</h2>
        <p className="text-sm text-muted-foreground">
          {t("pages.singbox.mapsTo", { tag: profile.keenetic_tag, interface: profile.interface })}
        </p>
      </div>
      <div className="flex flex-wrap gap-2">
        {(["direct", "selector", "urltest"] as const).map((mode) => (
          <Button key={mode} onClick={() => select.mutate({ mode })} type="button" variant={profile.mode === mode ? "default" : "outline"}>
            {mode}
          </Button>
        ))}
        {profile.subscription_url ? (
          <Button onClick={() => refresh.mutate()} type="button" variant="outline">
            {t("pages.singbox.refresh")}
          </Button>
        ) : null}
      </div>
      {profile.last_error ? <p className="text-sm text-destructive">{profile.last_error}</p> : null}
      <ul className="space-y-2">
        {profile.connections.map((connection) => (
          <li className="flex flex-wrap items-center justify-between gap-2 text-sm" key={connection.id}>
            <button className="text-left" onClick={() => select.mutate({ active: connection.id, mode: "direct" })} type="button">
              <span className="font-medium">{connection.name}</span>
              <span className="text-muted-foreground">
                {" "}
                {connection.protocol} {connection.server}:{connection.port}
                {profile.active === connection.id ? ` · ${t("pages.singbox.active")}` : ""}
              </span>
            </button>
            <Button onClick={() => remove.mutate(connection.id)} type="button" variant="outline">
              {t("common.delete")}
            </Button>
          </li>
        ))}
      </ul>
    </section>
  )
}
