<div align="center">
  <img src="docs/static/logo.svg" alt="keen-pbr-sing logo" width="140" />

  <h1>keen-pbr-sing</h1>

  <p><strong>sing-box TUN routing for Keenetic.</strong></p>
  <p>I use this tree to send selected traffic through a sing-box TUN, to see per-outbound traffic, and to keep that tunnel forwarding on Keenetic.</p>

  <p>
    <a href="https://github.com/shendr404/keen-pbr-sing/releases">
      <img src="https://img.shields.io/github/v/release/shendr404/keen-pbr-sing?style=for-the-badge&sort=date" alt="Latest release" />
    </a>
  </p>
</div>

> keen-pbr-sing is an independent project. It is not an official Keenetic product and is not affiliated with or endorsed by Keenetic or Netcraze.

Based on [keen-pbr](https://github.com/maksimkurb/keen-pbr) by Maxim Kurbatov. The license is the original GNU GPL v3 (`LICENSE`). Copyright notices in the source stay with their authors.

## What I run

keen-pbr-sing is my Keenetic build of that routing daemon, aimed at a sing-box TUN:

- a sing-box TUN outbound, such as `sbtun`, that forwards and stays reachable
- per-outbound bytes, packets, rate, and active connections in the Web UI, `GET /api/runtime/outbound-traffic`, and `/metrics`
- Keenetic fixes for NDM `FORWARD` policy DROP, a TUN with no gateway, a missing dnsmasq `conf-script` line, and list downloads that come back as HTML

## Names that stay keen-pbr

I renamed the visible title to keen-pbr-sing. I did not rename the binary, the package, the init script, the config paths, or the API, so an existing router install keeps working:

| What you already have | Name that stays |
|---|---|
| opkg package | `keen-pbr` and `keen-pbr-headless` |
| binary | `/opt/usr/bin/keen-pbr` |
| init script | `/opt/etc/init.d/S80keen-pbr` |
| config | `/opt/etc/keen-pbr/config.json` |
| Web UI and API | `http://<router>:12121` |

The page title and the header say keen-pbr-sing. `opkg`, the init script, and the command you run are still `keen-pbr`.

## Документация и opkg-фид

Русская документация: https://shendr404.github.io/keen-pbr-sing/

Неподписанный opkg-фид Keenetic: `https://shendr404.github.io/keen-pbr-sing/keenetic/<arch>/`, где `<arch>` — `mipsel-3.4`, `mips-3.4`, `aarch64-3.10`, `armv7-3.2` или `x64-3.2`. Для установки используйте `opkg print-architecture`, добавьте соответствующий `src/gz` в `/opt/etc/opkg.conf` и выполните `opkg update && opkg install keen-pbr`. Для варианта без UI: `keen-pbr-headless`.

Пакеты и `SHA256SUMS` также доступны в [GitHub Releases](https://github.com/shendr404/keen-pbr-sing/releases). Фид неподписанный: проверяйте источник и контрольные суммы.

## Install

Install from this repository's releases: https://github.com/shendr404/keen-pbr-sing/releases

On Keenetic Entware, take the `keenetic_mipsel-3.4` ipk from the latest release. The package lists `/opt/etc/keen-pbr/config.json` as a conffile, so opkg keeps the file you already have. Copy it aside first anyway:

```sh
cp /opt/etc/keen-pbr/config.json /opt/etc/keen-pbr/config.json.bak
opkg install ./keen-pbr_*_keenetic_mipsel-3.4.ipk
```

Do not pass `--force-maintainer`: that flag replaces conffiles with the copy shipped in the package. If opkg says the same version is already installed, reinstall with `opkg install --force-reinstall` and still without `--force-maintainer`. The postinst script restarts `S80keen-pbr`. The Web UI is `http://<router>:12121` and the saved config stays at `/opt/etc/keen-pbr/config.json`.

## Traffic counters

Each configured outbound reports cumulative bytes and packets, a bytes/s rate over the last few seconds, and, when the kernel exposes conntrack marks, the number of active connections.

- An **interface** outbound (a sing-box TUN named `sbtun`, for example) is read from `/proc/net/dev`. `tx` is what the kernel sends into the interface and `rx` is what it receives back, so the counters show whether traffic is actually crossing that device.
- Any other outbound that has an fwmark is read from the iptables `MARK` rules that set that mark (`iptables-save -c`, the iptables backend). With connmark restore those rules count the packets that were classified, not the whole flow, and they restart when the rules are rebuilt. Receive counters are omitted in that case. nftables does not expose these per-rule counters.
- Connections are entries in `/proc/net/nf_conntrack` whose `mark` equals the outbound's fwmark.

The Web UI shows this on **Outbounds** (Traffic column) and on the overview cards. `GET /api/runtime/outbound-traffic` returns the same sample. `/metrics` adds `keen_pbr_outbound_rx_bytes_total`, `keen_pbr_outbound_tx_bytes_total`, the matching packet counters, `keen_pbr_outbound_*_bytes_per_second` and `keen_pbr_outbound_connections`.

### Keenetic TUN (`sbtun`)

NDM's filter `FORWARD` policy is DROP, so a packet routed into a sing-box TUN is thrown away and the reply never returns. For each interface outbound, keen-pbr inserts two accepts if they are not already there: LAN (`route.inbound_interfaces`, or `br0`) to the interface, and the interface back to the LAN for `RELATED,ESTABLISHED`. `/opt/etc/ndm/netfilter.d/51-keen-pbr-forward.sh` puts them back after NDM rebuilds the filter table. `ip6tables` is used only when `daemon.ipv6_enabled` is not false. Stop and uninstall delete the rules.

A TUN has no gateway. `gateway: "auto"` (or any gateway that is not actually on the device) used to leave the outbound unavailable forever. A TUN (`/sys/class/net/<iface>/tun_flags`) is routed as `default dev <iface>`. The same applies to another point-to-point interface when no gateway route exists. The overview and the Outbounds page say that the gateway was ignored.

### DNS for lists the ISP poisons

Routing a list through `sbtun` does not change where the names are resolved. If the ISP answers those names with the wrong addresses, the connection never enters the tunnel. Give the list a DNS server that is itself sent through the outbound, and a DNS rule for the list:

```json
{
  "dns": {
    "resolver_integration": "dnsmasq",
    "servers": [
      { "tag": "proxy_dns", "type": "static", "address": "1.1.1.1", "detour": "proxy" }
    ],
    "rules": [
      { "list": ["blocked-sites"], "servers": ["proxy_dns"] }
    ]
  },
  "route": {
    "rules": [
      { "list": ["blocked-sites"], "outbound": "proxy" }
    ]
  }
}
```

`detour` marks the DNS query with the outbound's fwmark, so the answer comes from the far side of the tunnel. If a list is routed through an interface, urltest, or icmptest outbound and no such DNS rule exists, keen-pbr warns when the config is checked.

## Sing-box links

I can add a sing-box node from the Web UI instead of a shell script. Open **Sing-box**, paste a `vless://`, `hysteria2://`, `trojan://`, `ss://`, or `vmess://` link, preview the server and port, name it, and save. The daemon writes `/opt/etc/sing-box/config.json`, keeps a private copy of the links in `/opt/etc/keen-pbr/sing-box-links.json`, runs `sing-box check`, and adds a keen-pbr `interface` outbound for the TUN (`sbtun` by default, gateway `auto`). If the check fails, the previous sing-box config is put back. Secrets stay out of the log and out of the profile list the UI shows.

A subscription URL can be a base64 list of links, a plain list, sing-box JSON, or Clash JSON. Refresh keeps manual nodes and replaces subscription nodes. Set an interval in hours for the daemon to refresh in the background, even with the UI closed or the REST API disabled. Set it to `0` for manual refresh only. The daemon checks for due subscriptions every 30 seconds, downloads one at a time with a 20-second transfer timeout and a 2 MiB limit, and retries automatic refreshes after five minutes on failure. Unchanged nodes do not trigger a config write or restart. Changed configs are backed up and checked before restarting sing-box; a failure or a missing selected node keeps the previous configuration.

The **Sing-box** page shows the last successful refresh, next automatic refresh and last error or warning. Refresh failures also appear in diagnostics. Unchanged refresh timestamps and retry status are kept in RAM to protect flash; after a daemon restart, scheduling falls back to the last saved timestamp.

To force a refresh, run `keen-pbr subscriptions refresh --all` (also the default without an ID) or `keen-pbr subscriptions refresh <id>`. Run as root with the daemon running. In a Keenetic crontab, use the full path `/opt/usr/bin/keen-pbr`. The command reports each result and exits nonzero if a refresh fails or the daemon is busy.

The package, binary, and API paths stay `keen-pbr`. These routes are under the same Web UI login: `GET /api/sing-box/profiles`, `POST /api/sing-box/preview`, `POST /api/sing-box/connections`, `POST /api/sing-box/connections/delete`, `POST /api/sing-box/profiles/select`, `POST /api/sing-box/subscriptions`, `POST /api/sing-box/subscriptions/refresh`. The profile list returns `has_subscription` instead of the private `subscription_url`.

### Ссылки sing-box

Соединение можно добавить из веб-интерфейса, без shell-скрипта. Откройте **Sing-box**, вставьте ссылку `vless://`, `hysteria2://`, `trojan://`, `ss://` или `vmess://`, посмотрите сервер и порт, задайте имя и сохраните. Демон записывает `/opt/etc/sing-box/config.json`, хранит ссылки отдельно в `/opt/etc/keen-pbr/sing-box-links.json`, запускает `sing-box check` и добавляет outbound типа `interface` для TUN (`sbtun`, шлюз `auto`). Если проверка не проходит, предыдущий конфиг sing-box возвращается. Секреты не пишутся в журнал и не показываются в списке узлов.

Подписка может быть base64-списком ссылок, обычным списком, JSON sing-box или JSON Clash. Ручные узлы остаются, узлы подписки заменяются. Укажите интервал в часах, чтобы демон обновлял подписку в фоне, даже без открытого интерфейса или при отключённом REST API. Интервал `0` оставляет только ручное обновление. Демон проверяет сроки раз в 30 секунд, загружает подписки по одной с таймаутом 20 секунд и лимитом 2 МиБ, а после ошибки повторяет попытку через пять минут. Если узлы не изменились, конфиг не записывается и sing-box не перезапускается. Перед применением изменений демон делает резервную копию и проверяет конфиг. При ошибке или исчезновении выбранного узла сохраняется прежний конфиг.

На странице **Sing-box** видны время последнего успешного обновления, следующее автообновление и последняя ошибка или предупреждение. Ошибки обновления также видны в диагностике. Время проверок без изменений и сведения о повторах держатся в памяти, чтобы не изнашивать флешку. После перезапуска демон берёт последнее сохранённое время.

Для принудительного обновления используйте `keen-pbr subscriptions refresh --all` (также по умолчанию без ID) или `keen-pbr subscriptions refresh <id>`. Команда требует прав root и запущенного демона. В crontab на Keenetic укажите полный путь `/opt/usr/bin/keen-pbr`. Команда выводит результат по каждой подписке и возвращает ненулевой код при ошибке или если демон занят.

Пакет, бинарник и пути API по-прежнему называются `keen-pbr`. Методы под той же авторизацией: `GET /api/sing-box/profiles`, `POST /api/sing-box/preview`, `POST /api/sing-box/connections`, `POST /api/sing-box/connections/delete`, `POST /api/sing-box/profiles/select`, `POST /api/sing-box/subscriptions`, `POST /api/sing-box/subscriptions/refresh`. В списке профилей вместо закрытого `subscription_url` возвращается признак `has_subscription`.

## Documentation

[Документация на русском](https://shendr404.github.io/keen-pbr-sing/) · [GitHub Releases](https://github.com/shendr404/keen-pbr-sing/releases)

## License

GNU GPL v3. See [LICENSE](LICENSE).
