<div align="center">
  <img src="docs/static/logo.svg" alt="keen-pbr logo" width="140" />

  <h1>keen-pbr</h1>

  <p><strong>Policy-based routing for Linux routers.</strong></p>
  <p>Route selected traffic through VPN, WAN, or custom IP tables on <strong>OpenWrt</strong>, <strong>Keenetic</strong>, and <strong>Debian</strong> systems.</p>

  <p>
    <a href="https://github.com/maksimkurb/keen-pbr/actions/workflows/ci-packages.yml">
      <img src="https://img.shields.io/github/actions/workflow/status/maksimkurb/keen-pbr/.github%2Fworkflows%2Fci-packages.yml?style=for-the-badge&branch=main" alt="Build status" />
    </a>
    <a href="https://github.com/maksimkurb/keen-pbr/releases">
      <img src="https://img.shields.io/github/v/release/maksimkurb/keen-pbr?style=for-the-badge&sort=date" alt="Latest release" />
    </a>
    <br>
    <a href="https://keen-pbr.fyi/">
      <img src="https://img.shields.io/badge/docs-keen--pbr.fyi-0f766e?style=for-the-badge" alt="Documentation" />
    </a>
    <a href="https://t.me/keen_pbr">
      <img src="https://img.shields.io/badge/Telegram-Community-229ED9?style=for-the-badge&logo=telegram&logoColor=white" alt="Telegram community" />
    </a>
  </p>
</div>

> keen-pbr is an independent open-source project. It is not an official Keenetic product and is not affiliated with or endorsed by Keenetic or Netcraze.

## What It Does

keen-pbr is a policy-based routing daemon that selectively sends traffic through specific outbounds based on domain lists, IP ranges, ports, and addresses.

It is built for embedded Linux routers and can:

- route traffic through VPN, WAN, blackhole, or custom routing tables
- use failover chains and health checks to keep traffic on healthy outbounds
- learn domain IPs from DNS/L7 interception without configuring or requiring any resolver
- provide an optional web UI and HTTP API for management and diagnostics

## Traffic counters

Each configured outbound reports cumulative bytes and packets, a bytes/s rate over the last few seconds, and, when the kernel exposes conntrack marks, the number of active connections.

- An **interface** outbound (a sing-box TUN named `sbtun`, for example) is read from `/proc/net/dev`. `tx` is what the kernel sends into the interface and `rx` is what it receives back, so the counters show whether traffic is actually crossing that device.
- Any other outbound that has an fwmark is read from the iptables `MARK` rules that set that mark (`iptables-save -c`, the iptables backend). With connmark restore those rules count the packets that were classified, not the whole flow, and they restart when the rules are rebuilt. Receive counters are omitted in that case. nftables does not expose these per-rule counters.
- Connections are entries in `/proc/net/nf_conntrack` whose `mark` equals the outbound's fwmark.

The Web UI shows this on **Outbounds** (Traffic column) and on the overview cards. `GET /api/runtime/outbound-traffic` returns the same sample. `/metrics` adds `keen_pbr_outbound_rx_bytes_total`, `keen_pbr_outbound_tx_bytes_total`, the matching packet counters, `keen_pbr_outbound_*_bytes_per_second` and `keen_pbr_outbound_connections`.

## Install over Entware keen-pbr

The package lists `/opt/etc/keen-pbr/config.json` as a conffile, so opkg keeps the file you already have. Copy it aside first anyway:

```sh
cp /opt/etc/keen-pbr/config.json /opt/etc/keen-pbr/config.json.bak
opkg install ./keen-pbr_*_keenetic_mipsel-3.4.ipk
```

Do not pass `--force-maintainer`: that flag replaces conffiles with the copy shipped in the package. If opkg says the same version is already installed, reinstall with `opkg install --force-reinstall` and still without `--force-maintainer`. The postinst script restarts `S80keen-pbr`. The Web UI is `http://<router>:12121` and the saved config stays at `/opt/etc/keen-pbr/config.json`.

Build the mipsel-3.4 package with the Entware builder (iptables backend, the image keen-pbr already uses):

```sh
make frontend-build FRONTEND_PLATFORM=keenetic
make keenetic-packages KEENETIC_CONFIG=mipsel-3.4 KEENETIC_VERSION=current
```

The ipk is written under `build/packages/keenetic/current/mipsel/`.

## Upstream

This repository is [keen-pbr-sing](https://github.com/shendr404/keen-pbr-sing). keen-pbr itself is written by Maxim Kurbatov. The license is the original GNU GPL v3 (`LICENSE`).

To rebase onto later upstream commits, the `upstream` remote is:

```sh
git remote add upstream https://github.com/maksimkurb/keen-pbr.git
git fetch upstream
git rebase upstream/main
```

## Documentation & Installation

### <a href="https://keen-pbr.fyi/">&gt; Documentation in English &lt;</a>
### <a href="https://keen-pbr.fyi/ru/">&gt; Документация на русском &lt;</a>

## Upgrade from keen-pbr 2.x

* [Upgrade from 2.x (in English)](http://keen-pbr.fyi/docs/getting-started/upgrade-from-2x/)
* [Обновление с версии 2.x (на русском)](http://keen-pbr.fyi/ru/docs/getting-started/upgrade-from-2x/)


## Community

| Need | Link |
|---|---|
| Telegram chat | https://t.me/keen_pbr |
| Bug reports and feature requests | https://github.com/maksimkurb/keen-pbr/issues |

## Support The Project

If keen-pbr helps you, you can support its development here:

- Ko-fi: https://ko-fi.com/keen_pbr
- CloudTips: https://pay.cloudtips.ru/p/a633c47d

<!-- SPONSORS-LIST:START -->
# Thanks to all our supporters

## GitHub supporters

[<img src="https://github.com/lekobyroxa.png?size=96" alt="lekobyroxa" width="80" height="80" />](https://github.com/lekobyroxa)

<!-- SPONSORS-LIST:END -->

If you support keen-pbr and would like your name included, email
[keen-pbr@kurb.me](mailto:keen-pbr@kurb.me) or message
[tg.me/maksimkurb](https://t.me/maksimkurb).

## License

See [LICENSE](LICENSE).
