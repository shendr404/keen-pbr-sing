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

## Documentation

The shared configuration manual is still the keen-pbr documentation:

### <a href="https://keen-pbr.fyi/">&gt; Documentation in English &lt;</a>
### <a href="https://keen-pbr.fyi/ru/">&gt; Документация на русском &lt;</a>

## License

GNU GPL v3. See [LICENSE](LICENSE).
