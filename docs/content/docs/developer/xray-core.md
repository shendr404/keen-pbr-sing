---
title: Xray core selection
weight: 20
---

Design for a second proxy core. sing-box stays the default. Xray is optional and is not shipped inside keen-pbr-sx.

Checked against Xray-core `v26.10.29` (the Entware `xray-core` build of 26 November 2026 for `mipsel-3.4` and `aarch64-3.10`) and the TUN inbound described in `proxy/tun/README.md` on `XTLS/Xray-core` main.

## How sing-box is wired today

Each saved node becomes one TUN and one keen-pbr-sx `interface` outbound. The Outbounds page owns groups. A `urltest` or `icmptest` group is a keen-pbr-sx outbound whose members are those interface tags. keen-pbr-sx does not put a new node into a group by itself.

The link is parsed into a sing-box 1.14 outbound and stored, together with the original link, in `/opt/etc/keen-pbr-sx/sing-box-links.json`. The parser accepts VLESS, VMess, Trojan, Shadowsocks and Hysteria2, and the transports `tcp`, `ws`, `grpc`, `http`, `httpupgrade` and `quic`. `xhttp` and `splithttp` are skipped. VLESS Encryption other than empty or `none` (including `mlkem768x25519plus`) is rejected and never becomes an outbound. A node that later disappears from a subscription stays, marked missing. A node the core cannot carry is kept and marked unsupported, with the reason shown on the row.

`SingboxLinkService::persist` merges managed objects into `/opt/etc/sing-box/config.json` and leaves the rest of the file alone. Managed tags use the `kpbr-` prefix:

- inbound `kpbr-tun-<id>`, type `tun`, `interface_name` `sbtun`, `sbtun2`, …, address `172.19.x.1/30`, MTU 1400, stack `gvisor` or `system`, `auto_route` and `strict_route` off;
- outbound `kpbr-n-<id>` copied from the parsed link;
- one route rule from that inbound to that outbound;
- `kpbr-direct` only when the file had no outbounds of its own.

`sing-box check -c` runs before the new file is kept. A failure restores `config.json.bak-keenpbr`. The first touch of a pre-existing file also keeps `config.json.bak-keenpbr-original`. Reload calls `/opt/etc/init.d/S99sing-box restart`, then `S98sing-box`. If neither script exists, the UI says the config is valid and must be restarted by hand. If the keen-pbr-sx side of the save fails, the link store, the sing-box file and `config.json` are restored together.

keen-pbr-sx then creates an `interface` outbound with the same tag and the TUN name, and no gateway. Policy routing sends the marked flow into that device. `urltest` does not talk to sing-box: it sends an HTTP probe with the outbound fwmark, and the kernel delivers it to the TUN. `icmptest` is the same with ICMP.

## What maps to Xray

Use one Xray process with one TUN inbound per node, the same `sbtunN` names, and routing rules `inboundTag` → `outboundTag`. keen-pbr-sx interface outbounds, groups and fwmark probes stay as they are. The core only has to own the device and forward packets.

Xray TUN inbound landed in `v26.1.23` (23 January 2026, PR 5464) for Linux, Windows, Android and macOS. That first cut used gVisor. `v26.10.17` (12 October 2026) removed gVisor and replaced it with Xray's own TCP/IP stack; the project describes that as a smaller binary and about twice the throughput. Entware currently ships `xray-core` `26.10.29-1` for both `mipsel-3.4` and `aarch64-3.10`, so the build we would depend on is already past the gVisor cut. The installed size of that package is about 28 MiB on aarch64 and about 33 MiB on mipsel. That is disk as well as the RAM the process will touch.

Linux TUN behaviour that the generator must follow:

- `protocol` is `tun`. `settings.name` is the interface (`sbtun`, `sbtun2`, …). `settings.mtu` is the node MTU.
- Without `settings.gateway`, Linux brings the device up and assigns no address. Set `gateway` to the same `172.19.x.1/30` sing-box writes into `address`, so the device is usable as a route target.
- `autoSystemRoutingTable` stays false. keen-pbr-sx owns the policy rules. Xray's own table would fight those rules and can loop.
- `autoSystemDnsToGateway` stays false. It needs systemd-resolved and refuses to start on OpenWrt-style dnsmasq. Keenetic is in that class.
- Xray dials its upstream from the router. Those sockets must follow the main table out of the WAN, not back into `sbtunN`. The same constraint already exists for sing-box (`auto_detect_interface`, `auto_route` off).

`urltest` keeps working: the probe is an fwmark on a TCP connection, and any process that reads the TUN and proxies it will carry the probe. `icmptest` does not. Xray's TUN stack answers ICMP echo locally and does not check that the remote host answers. An `icmptest` group member whose device is an Xray TUN would look healthy without measuring the proxy. The UI should say that when the core is Xray.

Do not use socks or dokodemo plus tun2socks for the MVP. That adds a second moving part, a userspace TCP/IP stack outside Xray, and a different failure mode, while Xray 26.10 already has a router-oriented TUN. tun2socks remains a fallback only if a device cannot create `/dev/net/tun` or the 26.10 inbound misbehaves on mipsel. That is a later experiment, not the design.

### Protocols

| Node | sing-box 1.14 in this tree | Xray 26.10.29 |
| --- | --- | --- |
| VLESS TCP | yes | yes |
| VLESS Reality | yes | yes |
| VLESS Encryption (`mlkem768x25519plus` and any value other than empty/`none`) | rejected | yes; this is a reason to offer Xray |
| Reality X25519MLKEM768 key share | not generated | yes in current Xray |
| `xhttp`, `splithttp` | skipped | yes (`xhttp`) |
| `ws`, `grpc`, `http`, `httpupgrade`, `quic` | yes | yes |
| VMess, Trojan, Shadowsocks | yes | yes |
| Hysteria2 | yes | yes since `v26.3.27` (outbound in 26.1.23, inbound and transport completed in 26.3.27). Hysteria 1 is rejected by Xray (`version` must be 2) |

The UI already disables a subscription row and shows `unsupported_reason`. The same row is used per core: a node the selected core cannot render is disabled in the picker, and an interface that already exists is kept and marked unsupported instead of deleted. Copy stays explicit: "not supported by sing-box" or "not supported by Xray", not a generic failure.

## Config and switching

One setting for the whole router, not per node:

```json
{
  "proxy": {
    "core": "sing-box",
    "xray_bin": ""
  }
}
```

`core` is `sing-box` (default) or `xray`. An empty `xray_bin` is searched as `/opt/bin/xray`, `/opt/usr/bin/xray`, then `PATH`. A set path is used as given.

Generated Xray config: `/opt/etc/keen-pbr-sx/xray.json`. It is ours. `/opt/etc/xray/config.json` belongs to the Entware package and is not overwritten. The Entware init (`S24xray` or whichever script that package ships) is stopped and not enabled while keen-pbr-sx runs Xray, so two processes do not both open `sbtun`. keen-pbr-sx starts `xray run -c /opt/etc/keen-pbr-sx/xray.json` itself, detached with `start-stop-daemon -b`, same rule as the package restart: no `setsid` and no `nohup`.

Switch, in order:

1. Refuse if `core` is `xray` and the binary is missing, or `xray version` is older than `26.10.17`.
2. Re-parse each stored `link` into that core's outbound. Do not translate the stored sing-box JSON.
3. Write the new core file beside the old one. `xray run -test -c` or `sing-box check -c` must pass, or nothing else changes.
4. Stop the core that is running. Start the selected one.
5. Leave keen-pbr-sx interface outbounds in place when the TUN names did not change.
6. If start fails, delete or restore the new file, start the previous core again, and return the previous `core` value. The link store is not rewritten until the new process is up.

Rollback is the same sequence run backwards. A failed `check` never stops the live process.

## Packaging and the RAM warning

Xray is an optional Entware dependency, not a file in this repository. The packages to install are `xray` and `xray-core` (`opkg install xray`). `xray` is the init and config skeleton; `xray-core` is the binary. Detect with `xray version` and show the parsed version on the Sing-box page next to the core control. Below `26.10.17` the control explains that TUN still used gVisor and the switch stays disabled.

Read `MemTotal` from `/proc/meminfo`. If it is under 384 MiB, the core control shows a warning and does not change the selection by itself. Hopper KN-3810 (MIPS, about 254 MiB) is under that line. Netcraze Giga and Titan (aarch64, 512 MiB–1 GiB) are not. The warning text names the figure it saw. The user can still switch; the default remains sing-box.

## Recommendation

One core for the whole router. Not a hybrid, and not a per-node core, in the first version.

The TUN name is already the contract with policy routing, so a second process is possible later. It is the wrong default. Two cores means two resident binaries, two config writers, two restarts, and a split set of unsupported nodes. On the machines that need sing-box, the extra process is the thing we are trying to avoid. Hysteria2 no longer forces the split: Xray 26.10 speaks it. The nodes that still need Xray are the new VLESS features sing-box 1.14 does not implement.

Phase 1 is VLESS, including Reality, `xhttp` and VLESS Encryption, on that single Xray process, with the other protocols left unsupported while `core` is `xray`. They keep working when `core` is `sing-box`. Phase 2 maps VMess, Trojan, Shadowsocks, Hysteria2 and the remaining transports. Phase 3, only if a real config needs both cores at once, would be a per-node core. Do not start there.

Phase 1 is larger than a single safe change: a second renderer, process ownership, version detection, the RAM warning, and rollback. This document stops before that code.
