import { expect, test } from "bun:test"
import { createInstance } from "i18next"
import { I18nextProvider, initReactI18next } from "react-i18next"
import { renderToStaticMarkup } from "react-dom/server"

import { OutboundTrafficFigures } from "../src/components/shared/outbound-traffic"
import { formatTrafficBytes, type OutboundTrafficView } from "../src/components/shared/outbound-traffic-utils"
import { enTranslation } from "../src/i18n/en"

const i18n = createInstance()
await i18n.use(initReactI18next).init({
  lng: "en",
  resources: { en: { translation: enTranslation } },
})

function render(traffic: OutboundTrafficView) {
  return renderToStaticMarkup(
    <I18nextProvider i18n={i18n}>
      <OutboundTrafficFigures t={i18n.t.bind(i18n)} traffic={traffic} />
    </I18nextProvider>
  )
}

test("byte formatter uses binary units", () => {
  expect(formatTrafficBytes(0)).toBe("0 B")
  expect(formatTrafficBytes(1536)).toBe("1.50 KB")
  expect(formatTrafficBytes(4000)).toBe("3.91 KB")
})

test("interface outbound shows both directions, the rate and connections", () => {
  const html = render({
    source: "interface",
    interfaceName: "sbtun",
    interfacePresent: true,
    rxBytes: 1000,
    txBytes: 4000,
    rxBytesPerSec: 100,
    txBytesPerSec: 250,
    connections: 4,
  })
  expect(html).toContain("sbtun")
  expect(html).toContain("1000 B")
  expect(html).toContain("3.91 KB")
  expect(html).toContain("250 B")
  expect(html).toContain("4 conn")
})

test("a missing interface is reported instead of a zero counter", () => {
  const html = render({
    source: "interface",
    interfaceName: "sbtun",
    interfacePresent: false,
  })
  expect(html).toContain("sbtun is not present")
  expect(html).not.toContain("0 B")
})

test("fwmark outbound shows classified bytes and omits a receive counter", () => {
  const html = render({
    source: "fwmark",
    txBytes: 3400,
    txPackets: 12,
    txBytesPerSec: 40,
    connections: 1,
  })
  expect(html).toContain("classified")
  expect(html).toContain("3.32 KB")
  expect(html).toContain("12 pkt")
  expect(html).toContain("40 B/s")
  expect(html).toContain("1 conn")
  expect(html).not.toContain("↓")
})
