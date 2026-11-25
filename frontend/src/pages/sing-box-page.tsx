import { useEffect, useRef, useState } from "react"
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query"
import { useTranslation } from "react-i18next"

import type { ApiError } from "@/api/client"
import {
  fetchSingboxProfiles,
  postSingbox,
  type SingboxProfile,
  type SingboxState,
} from "@/api/sing-box"
import { PageHeader } from "@/components/shared/page-header"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { getApiErrorMessage } from "@/lib/api-errors"

const queryKey = ["sing-box-profiles"]

type Draft = {
  id: string
  name: string
  tag: string
  iface: string
  mode: string
  urltestUrl: string
  urltestInterval: string
  tunStack: string
  tunMtu: string
  tunAddress: string
  seeds: string
}

const nativeSelectClass =
  "h-8 w-full rounded-lg border border-input bg-background px-2.5 text-sm text-foreground scheme-light dark:bg-popover dark:text-popover-foreground dark:scheme-dark"

const emptyDraft = (): Draft => ({
  id: "",
  name: "",
  tag: "proxy",
  iface: "",
  mode: "selector",
  urltestUrl: "https://www.gstatic.com/generate_204",
  urltestInterval: "3m",
  tunStack: "gvisor",
  tunMtu: "1400",
  tunAddress: "",
  seeds: "",
})

function draftFromProfile(profile: SingboxProfile): Draft {
  return {
    id: profile.id,
    name: profile.name,
    tag: profile.keenetic_tag,
    iface: profile.interface,
    mode: profile.mode,
    urltestUrl: profile.urltest_url,
    urltestInterval: profile.urltest_interval || "3m",
    tunStack: profile.tun_stack || "gvisor",
    tunMtu: String(profile.tun_mtu || 1400),
    tunAddress: profile.tun_address || "",
    seeds: "",
  }
}

function formatRefreshTime(timestamp: number | null | undefined, locale: string) {
  return timestamp ? new Date(timestamp * 1000).toLocaleString(locale) : "—"
}

function localizeServerMessage(
  message: string,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  const skipped = /^(\d+) nodes skipped: transport (.+) is not supported by sing-box$/.exec(message)
  if (skipped) {
    return t("pages.singbox.skippedTransports", {
      count: Number(skipped[1]),
      transports: skipped[2],
    })
  }
  if (message.includes("vless encryption is ignored")) return t("pages.singbox.encryptionIgnored")
  return message
}

function noticeText(
  state: SingboxState,
  t: (key: string, options?: Record<string, unknown>) => string,
  fallback: string
) {
  const parts: string[] = []
  if (state.skipped_transports && state.skipped_transports.length > 0) {
    parts.push(
      t("pages.singbox.skippedTransports", {
        count: state.skipped ?? state.skipped_transports.length,
        transports: state.skipped_transports.join(", "),
      })
    )
  }
  if (state.encryption_ignored) parts.push(t("pages.singbox.encryptionIgnored"))
  if (state.warning) parts.push(localizeServerMessage(state.warning, t))
  return parts.join(" ") || fallback
}

function nonNegative(value: string) {
  const parsed = Number(value)
  if (!Number.isFinite(parsed) || parsed < 0) return 0
  return Math.trunc(parsed)
}

export function SingboxPage() {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const profiles = useQuery({ queryKey, queryFn: fetchSingboxProfiles, refetchInterval: 30_000 })
  const [draft, setDraft] = useState<Draft>(emptyDraft)
  const [ifaceTouched, setIfaceTouched] = useState(false)
  const [error, setError] = useState("")
  const [success, setSuccess] = useState("")
  const [focusProfileId, setFocusProfileId] = useState<string | null>(null)
  const createdId = useRef<string | null>(null)
  const scrolledId = useRef<string | null>(null)
  const suggestion = profiles.data?.suggested_interface ?? ""
  const shownIface = ifaceTouched ? draft.iface : suggestion || draft.iface

  const clearMessages = () => {
    setError("")
    setSuccess("")
  }
  const invalidate = () => queryClient.invalidateQueries({ queryKey })
  const fail = (caught: unknown) => {
    const message =
      caught && typeof caught === "object" && "message" in caught
        ? getApiErrorMessage(caught as ApiError)
        : t("pages.singbox.failed")
    setSuccess("")
    setError(localizeServerMessage(message, t))
  }
  const succeed = (state: SingboxState, fallback: string) => {
    setError("")
    setSuccess(noticeText(state, t, fallback))
  }

  const saveProfile = useMutation({
    mutationFn: async () => {
      const known = new Set((profiles.data?.profiles ?? []).map((profile) => profile.id))
      const state = await postSingbox("/api/sing-box/profiles", {
        id: draft.id,
        name: draft.name,
        tag: draft.tag,
        interface: shownIface,
        mode: draft.mode,
        urltest_url: draft.urltestUrl,
        urltest_interval: draft.urltestInterval,
        tun_stack: draft.tunStack,
        tun_mtu: nonNegative(draft.tunMtu) || 1400,
        tun_address: draft.tunAddress,
        links: draft.seeds,
      })
      const created = state.profiles.find((profile) => !known.has(profile.id)) ?? state.profiles.at(-1)
      createdId.current = created?.id ?? ""
      return state
    },
    onMutate: clearMessages,
    onSuccess: async (state) => {
      succeed(state, t("pages.singbox.savedProfile"))
      setIfaceTouched(false)
      setDraft(emptyDraft())
      setFocusProfileId(createdId.current)
      await invalidate()
    },
    onError: async (caught) => {
      fail(caught)
      if (createdId.current) setFocusProfileId(createdId.current)
      await invalidate()
    },
  })

  useEffect(() => {
    if (!focusProfileId || scrolledId.current === focusProfileId) return
    const node = document.getElementById(`singbox-links-${focusProfileId}`)
    if (!node) return
    scrolledId.current = focusProfileId
    node.scrollIntoView({ behavior: "smooth", block: "center" })
    node.focus()
  }, [focusProfileId, profiles.data])

  const updateDraft = (patch: Partial<Draft>) => {
    clearMessages()
    if ("iface" in patch) setIfaceTouched(true)
    setDraft((current) => ({ ...current, ...patch }))
  }

  return (
    <div className="space-y-6">
      <PageHeader description={t("pages.singbox.description")} title={t("pages.singbox.title")} />
      <Messages error={error} onDismiss={clearMessages} success={success} />
      <section className="space-y-4 rounded-lg border p-4">
        <h2 className="text-base font-medium">{t("pages.singbox.create")}</h2>
        <ProfileFields
          draft={{ ...draft, iface: shownIface }}
          idPrefix="create"
          onChange={updateDraft}
        />
        <div className="space-y-1.5">
          <Label htmlFor="create-seeds">{t("pages.singbox.seeds")}</Label>
          <textarea
            className="min-h-28 w-full rounded-lg border border-input bg-transparent p-2.5 font-mono text-sm"
            id="create-seeds"
            onChange={(event) => updateDraft({ seeds: event.target.value })}
            placeholder={t("pages.singbox.seedsPlaceholder")}
            value={draft.seeds}
          />
          <p className="text-xs text-muted-foreground">{t("pages.singbox.seedsHint")}</p>
        </div>
        <Button disabled={saveProfile.isPending || draft.tag.length === 0} onClick={() => saveProfile.mutate()} type="button">
          {t("pages.singbox.saveProfile")}
        </Button>
      </section>
      {profiles.data && profiles.data.profiles.length === 0 ? (
        <p className="text-sm text-muted-foreground">{t("pages.singbox.noProfiles")}</p>
      ) : null}
      {profiles.data?.profiles.map((profile) => (
        <ProfileCard
          key={profile.id}
          onError={fail}
          onStart={clearMessages}
          onSuccess={async (state, fallback) => {
            succeed(state, fallback)
            await invalidate()
          }}
          profile={profile}
        />
      ))}
    </div>
  )
}

function Messages({
  error,
  success,
  onDismiss,
}: {
  error: string
  success: string
  onDismiss: () => void
}) {
  const { t } = useTranslation()
  if (!error && !success) return null
  return (
    <div className="space-y-2">
      {error ? (
        <Alert variant="destructive">
          <AlertDescription className="flex items-start justify-between gap-3">
            <span>{error}</span>
            <Button onClick={onDismiss} size="sm" type="button" variant="ghost">
              {t("pages.singbox.dismiss")}
            </Button>
          </AlertDescription>
        </Alert>
      ) : null}
      {success ? (
        <Alert>
          <AlertDescription className="flex items-start justify-between gap-3">
            <span>{success}</span>
            <Button onClick={onDismiss} size="sm" type="button" variant="ghost">
              {t("pages.singbox.dismiss")}
            </Button>
          </AlertDescription>
        </Alert>
      ) : null}
    </div>
  )
}

function ProfileFields({
  draft,
  idPrefix,
  onChange,
}: {
  draft: Draft
  idPrefix: string
  onChange: (patch: Partial<Draft>) => void
}) {
  const { t } = useTranslation()
  const fieldId = (name: string) => `${idPrefix}-${name}`
  return (
    <div className="grid gap-3 md:grid-cols-2">
      <div className="space-y-1.5">
        <Label htmlFor={fieldId("name")}>{t("pages.singbox.name")}</Label>
        <Input id={fieldId("name")} onChange={(event) => onChange({ name: event.target.value })} value={draft.name} />
      </div>
      <div className="space-y-1.5">
        <Label htmlFor={fieldId("tag")}>{t("pages.singbox.tag")}</Label>
        <Input id={fieldId("tag")} onChange={(event) => onChange({ tag: event.target.value })} value={draft.tag} />
      </div>
      <div className="space-y-1.5">
        <Label htmlFor={fieldId("interface")}>{t("pages.singbox.interface")}</Label>
        <Input
          id={fieldId("interface")}
          onChange={(event) => onChange({ iface: event.target.value })}
          placeholder={t("pages.singbox.addressPlaceholder")}
          value={draft.iface}
        />
        <p className="text-xs text-muted-foreground">{t("pages.singbox.interfaceHint")}</p>
      </div>
      <div className="space-y-1.5">
        <Label htmlFor={fieldId("mode")}>{t("pages.singbox.mode")}</Label>
        <select
          className={nativeSelectClass}
          id={fieldId("mode")}
          onChange={(event) => onChange({ mode: event.target.value })}
          value={draft.mode}
        >
          <option value="direct">{t("pages.singbox.modes.direct")}</option>
          <option value="selector">{t("pages.singbox.modes.selector")}</option>
          <option value="urltest">{t("pages.singbox.modes.urltest")}</option>
        </select>
      </div>
      {draft.mode === "urltest" ? (
        <>
          <div className="space-y-1.5">
            <Label htmlFor={fieldId("url")}>{t("pages.singbox.urltestUrl")}</Label>
            <Input id={fieldId("url")} onChange={(event) => onChange({ urltestUrl: event.target.value })} value={draft.urltestUrl} />
          </div>
          <div className="space-y-1.5">
            <Label htmlFor={fieldId("probe")}>{t("pages.singbox.urltestInterval")}</Label>
            <Input
              id={fieldId("probe")}
              onChange={(event) => onChange({ urltestInterval: event.target.value })}
              value={draft.urltestInterval}
            />
          </div>
        </>
      ) : null}
      <details className="rounded-lg border border-dashed px-3 py-2 md:col-span-2">
        <summary className="cursor-pointer text-sm font-medium">{t("pages.singbox.advanced")}</summary>
        <div className="mt-3 grid gap-3 md:grid-cols-2">
          <div className="space-y-1.5">
            <Label htmlFor={fieldId("stack")}>{t("pages.singbox.tunStack")}</Label>
            <select
              className={nativeSelectClass}
              id={fieldId("stack")}
              onChange={(event) => onChange({ tunStack: event.target.value })}
              value={draft.tunStack}
            >
              <option value="gvisor">gvisor</option>
              <option value="system">system</option>
            </select>
          </div>
          <div className="space-y-1.5">
            <Label htmlFor={fieldId("mtu")}>{t("pages.singbox.tunMtu")}</Label>
            <Input
              id={fieldId("mtu")}
              max={65535}
              min={576}
              onChange={(event) => onChange({ tunMtu: event.target.value })}
              type="number"
              value={draft.tunMtu}
            />
          </div>
          <div className="space-y-1.5 md:col-span-2">
            <Label htmlFor={fieldId("address")}>{t("pages.singbox.tunAddress")}</Label>
            <Input
              id={fieldId("address")}
              onChange={(event) => onChange({ tunAddress: event.target.value })}
              placeholder={t("pages.singbox.addressPlaceholder")}
              value={draft.tunAddress}
            />
            <p className="text-xs text-muted-foreground">{t("pages.singbox.tunAddressHint")}</p>
          </div>
        </div>
      </details>
    </div>
  )
}

function ProfileCard({
  profile,
  onStart,
  onSuccess,
  onError,
}: {
  profile: SingboxProfile
  onStart: () => void
  onSuccess: (state: SingboxState, fallback: string) => Promise<void>
  onError: (error: unknown) => void
}) {
  const { t, i18n } = useTranslation()
  const [editing, setEditing] = useState(false)
  const [draft, setDraft] = useState(() => draftFromProfile(profile))
  const [links, setLinks] = useState("")
  const [subscriptionUrl, setSubscriptionUrl] = useState("")
  const [intervalHours, setIntervalHours] = useState("0")
  const subscriptions = profile.subscriptions ?? []
  const current = profile.connections.find((connection) => connection.id === profile.urltest_now)

  const select = useMutation({
    mutationFn: (active: string) =>
      postSingbox("/api/sing-box/profiles/select", { profile_id: profile.id, mode: profile.mode, active }),
    onMutate: onStart,
    onSuccess: (state) => onSuccess(state, t("pages.singbox.saved")),
    onError,
  })
  const remove = useMutation({
    mutationFn: (id: string) => postSingbox("/api/sing-box/connections/delete", { profile_id: profile.id, id }),
    onMutate: onStart,
    onSuccess: (state) => onSuccess(state, t("pages.singbox.saved")),
    onError,
  })
  const addLinks = useMutation({
    mutationFn: () => postSingbox("/api/sing-box/connections", { profile_id: profile.id, links }),
    onMutate: onStart,
    onSuccess: async (state) => {
      setLinks("")
      await onSuccess(state, t("pages.singbox.proxiesSaved"))
    },
    onError,
  })
  const attach = useMutation({
    mutationFn: () =>
      postSingbox("/api/sing-box/subscriptions", {
        profile_id: profile.id,
        url: subscriptionUrl,
        interval_hours: nonNegative(intervalHours),
      }),
    onMutate: onStart,
    onSuccess: async (state) => {
      setSubscriptionUrl("")
      await onSuccess(state, t("pages.singbox.subscriptionSaved"))
    },
    onError,
  })
  const refresh = useMutation({
    mutationFn: (subscriptionId: string) =>
      postSingbox("/api/sing-box/subscriptions/refresh", {
        profile_id: profile.id,
        subscription_id: subscriptionId,
      }),
    onMutate: onStart,
    onSuccess: (state) => onSuccess(state, t("pages.singbox.saved")),
    onError,
  })
  const detach = useMutation({
    mutationFn: (subscriptionId: string) =>
      postSingbox("/api/sing-box/subscriptions/delete", {
        profile_id: profile.id,
        subscription_id: subscriptionId,
      }),
    onMutate: onStart,
    onSuccess: (state) => onSuccess(state, t("pages.singbox.saved")),
    onError,
  })
  const saveEdit = useMutation({
    mutationFn: () =>
      postSingbox("/api/sing-box/profiles", {
        id: profile.id,
        name: draft.name,
        tag: draft.tag,
        interface: draft.iface,
        mode: draft.mode,
        urltest_url: draft.urltestUrl,
        urltest_interval: draft.urltestInterval,
        tun_stack: draft.tunStack,
        tun_mtu: nonNegative(draft.tunMtu) || 1400,
        tun_address: draft.tunAddress,
      }),
    onMutate: onStart,
    onSuccess: async (state) => {
      setEditing(false)
      await onSuccess(state, t("pages.singbox.savedProfile"))
    },
    onError,
  })
  const removeProfile = useMutation({
    mutationFn: () => postSingbox("/api/sing-box/profiles/delete", { profile_id: profile.id }),
    onMutate: onStart,
    onSuccess: (state) => onSuccess(state, t("pages.singbox.saved")),
    onError,
  })

  return (
    <section className="space-y-4 rounded-lg border p-4">
      <div className="flex flex-wrap items-baseline justify-between gap-2">
        <h2 className="text-base font-medium">{profile.name}</h2>
        <p className="text-sm text-muted-foreground">
          {t("pages.singbox.mapsTo", { tag: profile.keenetic_tag, interface: profile.interface })}
        </p>
      </div>
      <p className="text-sm text-muted-foreground">
        {t(`pages.singbox.modes.${profile.mode}`, { defaultValue: profile.mode })}
        {" · "}
        {profile.tun_stack || "gvisor"}
        {" · MTU "}
        {profile.tun_mtu || 1400}
      </p>
      {profile.mode === "urltest" ? (
        <p className="text-sm">
          {current
            ? t("pages.singbox.urltestCurrent", { name: current.name })
            : t("pages.singbox.urltestAuto", {
                interval: profile.urltest_interval || "3m",
                url: profile.urltest_url,
              })}
        </p>
      ) : null}
      <div className="flex flex-wrap gap-2">
        <Button
          onClick={() => {
            setDraft(draftFromProfile(profile))
            setEditing((value) => !value)
          }}
          type="button"
          variant="outline"
        >
          {t("pages.singbox.edit")}
        </Button>
        <Button disabled={removeProfile.isPending} onClick={() => removeProfile.mutate()} type="button" variant="outline">
          {t("pages.singbox.deleteProfile")}
        </Button>
      </div>
      {editing ? (
        <div className="space-y-3">
          <ProfileFields
            draft={draft}
            idPrefix={profile.id}
            onChange={(patch) => {
              onStart()
              setDraft((currentDraft) => ({ ...currentDraft, ...patch }))
            }}
          />
          <Button disabled={saveEdit.isPending} onClick={() => saveEdit.mutate()} type="button">
            {t("pages.singbox.saveProfile")}
          </Button>
        </div>
      ) : null}
      <div className="space-y-3 rounded-lg bg-muted/40 p-3" id={`singbox-nodes-${profile.id}`}>
        <div className="space-y-1">
          <h3 className="text-base font-medium">{t("pages.singbox.nodes")}</h3>
          <p className="text-sm text-muted-foreground">{t("pages.singbox.nodesHint")}</p>
        </div>
        <ul className="space-y-2">
          {profile.connections.map((connection) => {
            const selected =
              profile.mode === "urltest"
                ? profile.urltest_now === connection.id
                : profile.active === connection.id
            const mark =
              profile.mode === "urltest"
                ? selected
                  ? t("pages.singbox.current")
                  : ""
                : selected
                  ? t("pages.singbox.active")
                  : ""
            return (
              <li className="flex flex-wrap items-center justify-between gap-2 text-sm" key={connection.id}>
                {profile.mode === "urltest" ? (
                  <div className="text-left">
                    <span className="font-medium">{connection.name}</span>
                    <span className="text-muted-foreground">
                      {" "}
                      {connection.protocol} {connection.server}:{connection.port}
                      {mark ? ` · ${mark}` : ""}
                    </span>
                  </div>
                ) : (
                  <button
                    className="text-left"
                    onClick={() => select.mutate(connection.id)}
                    type="button"
                  >
                    <span className="font-medium">{connection.name}</span>
                    <span className="text-muted-foreground">
                      {" "}
                      {connection.protocol} {connection.server}:{connection.port}
                      {mark ? ` · ${mark}` : ""}
                    </span>
                  </button>
                )}
                <Button onClick={() => remove.mutate(connection.id)} type="button" variant="outline">
                  {t("common.delete")}
                </Button>
              </li>
            )
          })}
        </ul>
        <div className="space-y-1">
          <Label htmlFor={`singbox-links-${profile.id}`}>{t("pages.singbox.seeds")}</Label>
          <textarea
            className="min-h-28 w-full rounded-lg border border-input bg-background p-2.5 font-mono text-sm"
            id={`singbox-links-${profile.id}`}
            onChange={(event) => {
              onStart()
              setLinks(event.target.value)
            }}
            placeholder={t("pages.singbox.seedsPlaceholder")}
            value={links}
          />
        </div>
        <Button disabled={addLinks.isPending || links.trim().length === 0} onClick={() => addLinks.mutate()} type="button">
          {t("pages.singbox.addProxies")}
        </Button>
      </div>
      <div className="space-y-3">
        <h3 className="text-sm font-medium">{t("pages.singbox.subscription")}</h3>
        {subscriptions.map((subscription) => (
          <div className="space-y-2 rounded-lg border p-3" key={subscription.id}>
            <dl className="grid grid-cols-[auto_1fr] gap-x-3 text-sm">
              <dt>{t("pages.singbox.interval")}</dt>
              <dd>{subscription.interval_hours}</dd>
              <dt>{t("pages.singbox.lastRefresh")}</dt>
              <dd>{formatRefreshTime(subscription.last_refresh, i18n.language)}</dd>
              <dt>{t("pages.singbox.nextRefresh")}</dt>
              <dd>
                {subscription.interval_hours > 0
                  ? formatRefreshTime(subscription.next_refresh, i18n.language)
                  : t("pages.singbox.autoRefreshOff")}
              </dd>
              <dt>{t("pages.singbox.lastError")}</dt>
              <dd className={subscription.last_error ? "text-destructive" : undefined}>
                {subscription.last_error ? localizeServerMessage(subscription.last_error, t) : "—"}
              </dd>
            </dl>
            <div className="flex flex-wrap gap-2">
              <Button disabled={refresh.isPending} onClick={() => refresh.mutate(subscription.id)} type="button" variant="outline">
                {t("pages.singbox.refresh")}
              </Button>
              <Button disabled={detach.isPending} onClick={() => detach.mutate(subscription.id)} type="button" variant="outline">
                {t("pages.singbox.removeSubscription")}
              </Button>
            </div>
          </div>
        ))}
        <div className="space-y-1">
          <Label htmlFor={`${profile.id}-sub-url`}>{t("pages.singbox.subscriptionUrl")}</Label>
          <Input
            id={`${profile.id}-sub-url`}
            onChange={(event) => {
              onStart()
              setSubscriptionUrl(event.target.value)
            }}
            placeholder="https://…"
            value={subscriptionUrl}
          />
        </div>
        <div className="space-y-1">
          <Label htmlFor={`${profile.id}-sub-hours`}>{t("pages.singbox.interval")}</Label>
          <Input
            id={`${profile.id}-sub-hours`}
            min={0}
            onChange={(event) => {
              onStart()
              setIntervalHours(event.target.value)
            }}
            type="number"
            value={intervalHours}
          />
        </div>
        <Button
          disabled={attach.isPending || subscriptionUrl.length === 0}
          onClick={() => attach.mutate()}
          type="button"
        >
          {t("pages.singbox.attachSubscription")}
        </Button>
      </div>
    </section>
  )
}
