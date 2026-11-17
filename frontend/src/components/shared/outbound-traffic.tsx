import type { TFunction } from "i18next"

export type OutboundTrafficApiEntry = {
  tag: string
  source: OutboundTrafficView["source"]
  interface_name?: string
  interface_present?: boolean
  rx_bytes?: number
  rx_packets?: number
  tx_bytes?: number
  tx_packets?: number
  rx_bytes_per_sec?: number
  tx_bytes_per_sec?: number
  connections?: number
}

export function outboundTrafficViews(entries: OutboundTrafficApiEntry[]) {
  return new Map(
    entries.map((entry) => [
      entry.tag,
      {
        source: entry.source,
        interfaceName: entry.interface_name,
        interfacePresent: entry.interface_present,
        rxBytes: entry.rx_bytes,
        rxPackets: entry.rx_packets,
        txBytes: entry.tx_bytes,
        txPackets: entry.tx_packets,
        rxBytesPerSec: entry.rx_bytes_per_sec,
        txBytesPerSec: entry.tx_bytes_per_sec,
        connections: entry.connections,
      } satisfies OutboundTrafficView,
    ])
  )
}

export type OutboundTrafficView = {
  source: "interface" | "fwmark" | "none"
  interfaceName?: string
  interfacePresent?: boolean
  rxBytes?: number
  rxPackets?: number
  txBytes?: number
  txPackets?: number
  rxBytesPerSec?: number
  txBytesPerSec?: number
  connections?: number
}

const UNITS = ["B", "KB", "MB", "GB", "TB"] as const

export function formatTrafficBytes(value: number): string {
  if (!Number.isFinite(value) || value < 0) return "0 B"
  let scaled = value
  let unit = 0
  while (scaled >= 1024 && unit < UNITS.length - 1) {
    scaled /= 1024
    unit += 1
  }
  const digits = unit === 0 ? 0 : scaled >= 100 ? 0 : scaled >= 10 ? 1 : 2
  return `${scaled.toFixed(digits)} ${UNITS[unit]}`
}

function formatRate(value: number, t: TFunction): string {
  return t("pages.outbounds.traffic.perSecond", {
    value: formatTrafficBytes(value),
  })
}

export function OutboundTrafficFigures({
  traffic,
  t,
  pending,
  failed,
}: {
  traffic?: OutboundTrafficView
  t: TFunction
  pending?: boolean
  failed?: boolean
}) {
  if (failed) {
    return (
      <span className="text-sm text-muted-foreground">
        {t("pages.outbounds.traffic.unavailable")}
      </span>
    )
  }
  if (pending && !traffic) {
    return (
      <span className="text-sm text-muted-foreground">
        {t("pages.outbounds.traffic.loading")}
      </span>
    )
  }
  if (!traffic || traffic.source === "none") {
    if (typeof traffic?.connections === "number") {
      return (
        <span
          className="text-sm text-muted-foreground tabular-nums"
          title={t("pages.outbounds.traffic.connectionsTitle")}
        >
          {t("pages.outbounds.traffic.connections", {
            count: traffic.connections,
          })}
        </span>
      )
    }
    return <span className="text-sm text-muted-foreground">—</span>
  }

  const title =
    traffic.source === "interface"
      ? t("pages.outbounds.traffic.interfaceTitle", {
          interface: traffic.interfaceName ?? "",
        })
      : t("pages.outbounds.traffic.fwmarkTitle")

  if (traffic.source === "interface" && traffic.interfacePresent === false) {
    return (
      <span className="text-sm text-muted-foreground" title={title}>
        {t("pages.outbounds.traffic.interfaceMissing", {
          interface: traffic.interfaceName ?? "",
        })}
      </span>
    )
  }

  const connections =
    typeof traffic.connections === "number"
      ? t("pages.outbounds.traffic.connections", { count: traffic.connections })
      : undefined

  if (traffic.source === "fwmark") {
    return (
      <div className="min-w-0 text-sm" title={title}>
        <div className="truncate tabular-nums">
          {t("pages.outbounds.traffic.classified", {
            bytes: formatTrafficBytes(traffic.txBytes ?? 0),
            packets: traffic.txPackets ?? 0,
          })}
        </div>
        <div className="truncate text-xs text-muted-foreground tabular-nums">
          {typeof traffic.txBytesPerSec === "number"
            ? formatRate(traffic.txBytesPerSec, t)
            : "—"}
          {connections ? ` · ${connections}` : ""}
        </div>
      </div>
    )
  }

  const rate =
    typeof traffic.rxBytesPerSec === "number" ||
    typeof traffic.txBytesPerSec === "number"
      ? [
          typeof traffic.rxBytesPerSec === "number"
            ? t("pages.outbounds.traffic.down", {
                value: formatRate(traffic.rxBytesPerSec, t),
              })
            : null,
          typeof traffic.txBytesPerSec === "number"
            ? t("pages.outbounds.traffic.up", {
                value: formatRate(traffic.txBytesPerSec, t),
              })
            : null,
        ]
          .filter(Boolean)
          .join(" ")
      : "—"

  return (
    <div className="min-w-0 text-sm" title={title}>
      <div className="truncate tabular-nums">
        {t("pages.outbounds.traffic.down", {
          value: formatTrafficBytes(traffic.rxBytes ?? 0),
        })}{" "}
        {t("pages.outbounds.traffic.up", {
          value: formatTrafficBytes(traffic.txBytes ?? 0),
        })}
      </div>
      <div
        className="truncate text-xs text-muted-foreground tabular-nums"
        title={
          connections
            ? t("pages.outbounds.traffic.connectionsTitle")
            : undefined
        }
      >
        {rate}
        {connections ? ` · ${connections}` : ""}
      </div>
    </div>
  )
}

export function outboundTrafficInline(
  traffic: OutboundTrafficView | undefined,
  t: TFunction
): string | undefined {
  if (!traffic) return undefined
  if (traffic.source === "interface" && traffic.interfacePresent === false) {
    return t("pages.outbounds.traffic.interfaceMissing", {
      interface: traffic.interfaceName ?? "",
    })
  }
  if (traffic.source === "interface") {
    if (
      typeof traffic.rxBytesPerSec === "number" ||
      typeof traffic.txBytesPerSec === "number"
    ) {
      return [
        t("pages.outbounds.traffic.down", {
          value: formatRate(traffic.rxBytesPerSec ?? 0, t),
        }),
        t("pages.outbounds.traffic.up", {
          value: formatRate(traffic.txBytesPerSec ?? 0, t),
        }),
      ].join(" ")
    }
    if (
      typeof traffic.rxBytes === "number" ||
      typeof traffic.txBytes === "number"
    ) {
      return [
        t("pages.outbounds.traffic.down", {
          value: formatTrafficBytes(traffic.rxBytes ?? 0),
        }),
        t("pages.outbounds.traffic.up", {
          value: formatTrafficBytes(traffic.txBytes ?? 0),
        }),
      ].join(" ")
    }
  }
  if (traffic.source === "fwmark" && typeof traffic.txBytesPerSec === "number") {
    return t("pages.outbounds.traffic.up", {
      value: formatRate(traffic.txBytesPerSec, t),
    })
  }
  if (traffic.source === "fwmark" && typeof traffic.txBytes === "number") {
    return t("pages.outbounds.traffic.classified", {
      bytes: formatTrafficBytes(traffic.txBytes),
      packets: traffic.txPackets ?? 0,
    })
  }
  return undefined
}
