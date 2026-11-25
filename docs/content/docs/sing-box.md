---
title: Sing-box profiles, nodes and subscriptions
weight: 7
---

The **Sing-box interfaces** page treats each proxy path as a profile: a keen-pbr outbound tag, a TUN interface name, and a node policy.

Create a profile, then add links (one per line) or attach subscriptions. The page registers an `interface` outbound **without** `gateway`. A TUN or point-to-point interface does not need one, and `auto` only produces a warning. Several of these interface outbounds can be members of a keen-pbr `urltest` or `icmptest` group.

Modes are `direct` (one node), `selector` (manual choice; clicking a node does not change the mode) and `urltest` (sing-box picks by latency). TUN defaults are stack `gvisor` and MTU 1400. An empty interface name becomes the next free name (`sbtun`, then `sbtun2`, and so on). An empty address becomes a free `172.19.x.1/30` that does not overlap an address already used by an inbound.

Supported share links are `vless://`, `vmess://`, `trojan://`, `ss://` and `hysteria2://`. sing-box 1.14 transports are `tcp`, `ws`, `grpc`, `http`, `httpupgrade` and `quic`. Lines that use `xhttp`, `splithttp` or another unknown transport are skipped, and the UI reports how many were skipped. VLESS Reality without `fp` gets `utls` fingerprint `chrome`. VLESS `encryption` is ignored.

Subscriptions may be a plain or base64 list, sing-box JSON, or Clash JSON. A profile can hold more than one. The interval is in hours; `0` is manual only. The daemon refreshes due subscriptions in the background (2 MiB, 20 seconds, five minutes between failures).

```sh
/opt/usr/bin/keen-pbr subscriptions refresh --all
/opt/usr/bin/keen-pbr subscriptions refresh <id>
```

`sing-box check` errors return the FATAL/ERROR line with uuids, passwords and keys redacted. A missing binary is reported separately.

## Existing `/opt/etc/sing-box/config.json`

keen-pbr does not replace the file. The first edit saves `config.json.bak-keenpbr-original` once. Each later edit refreshes `config.json.bak-keenpbr` and runs `sing-box check` before keeping the new file.

Only objects tagged `kpbr-` are managed (`kpbr-tun-<profile>`, `kpbr-n-…`, `kpbr-g-<profile>`, and `kpbr-direct` when the file had no outbounds of its own). `dns`, `log`, `experimental`, and every other inbound, outbound and route rule are preserved. That includes a mixed inbound on `192.168.1.1:2080`, a manual hysteria2 outbound, and an existing `sbtun` TUN with its addresses, MTU and stack.

If the profile's interface name is already used by an inbound keen-pbr does not own, the save is rejected and the file is left untouched. Pick another name, such as `sbtun2`. A working manual config used by a keen-pbr outbound such as `tun_hu` therefore survives the first save after upgrade.

Removing the last managed node deletes only `kpbr-` objects. A file that contained nothing else is removed; a file that still has unmanaged sections is written back and reloaded.

Links stay in `/opt/etc/keen-pbr/sing-box-links.json` and are not returned by the profile list.
