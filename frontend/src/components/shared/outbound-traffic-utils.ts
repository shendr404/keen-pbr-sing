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

export function formatRate(value: number, t: TFunction): string {
  return t("pages.outbounds.traffic.perSecond", {
    value: formatTrafficBytes(value),
  })
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
