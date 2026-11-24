import type { TFunction } from "i18next"

import { formatTrafficBytes, formatRate, type OutboundTrafficView } from "./outbound-traffic-utils"

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
