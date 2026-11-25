---
title: Sing-box — one interface per node
weight: 7
---

On the **Sing-box interfaces** page each proxy node is its own TUN. Switching and latency checks belong on the Outbounds page, where several interface outbounds can be members of a `urltest` or `icmptest` group.

## Interface

1. Install sing-box. A TUN does not have to exist beforehand: keen-pbr adds its own inbound.
2. Open **Sing-box interfaces**.
3. Paste one or more links, one per line, and save. Each link immediately becomes its own TUN and a keen-pbr `interface` outbound **without** a `gateway`.
4. The tag comes from the node name in the link (`hu-1` stays `hu-1`). A tag that is already used gets a `-2` suffix. The tag can be edited.
5. The TUN name is the next free one (`sbtun`, then `sbtun2`, …). A name already used by an unmanaged inbound is skipped. An empty address gets a free `172.19.x.1/30`. Stack and MTU are under Advanced.
6. On the Outbounds page, point a route rule at the tag or add several of these outbounds to a group. keen-pbr does not add them to a group by itself.

## Subscriptions

Load the node list, tick the nodes that should become interfaces, and confirm. The list shows name, protocol, server, and skipped lines (for example `xhttp`) with the reason. The subscription stores its URL, the update interval, and the selected node identities.

The interval is in **hours**. `0` means manual refresh only. The daemon checks every 30 seconds. A download is limited to 2 MiB and 20 seconds; after a failure the next attempt waits five minutes.

A refresh updates only nodes that already exist. They match by name, or by server, port and protocol when the name is empty. New subscription nodes are not added automatically: the card shows how many new nodes appeared and a button that opens the selection again. If a selected node disappears, its interface stays and is marked as missing from the subscription.

From SSH:

```sh
/opt/usr/bin/keen-pbr subscriptions refresh --all
/opt/usr/bin/keen-pbr subscriptions refresh <id>
```

Deleting an interface removes its TUN inbound, sing-box outbound, route rule and keen-pbr outbound. If a route rule or a `urltest`/`icmptest` group still references that outbound, deletion is refused and the error names them.

When `sing-box check` fails, the error contains the FATAL/ERROR line with uuids, passwords and keys redacted. A missing sing-box binary is reported separately from a configuration error.

## What happens to `/opt/etc/sing-box/config.json`

keen-pbr does **not** replace the file.

The first edit of an existing file keeps a one-time copy `config.json.bak-keenpbr-original`. Later edits refresh only `config.json.bak-keenpbr`. `sing-box check` runs before the new file is kept; a failure restores the rolling copy. If the keen-pbr outbound cannot be saved after that, the link store, the sing-box config and `config.json` are all restored.

Managed objects use the `kpbr-` prefix:

- inbound `kpbr-tun-<node id>`;
- outbound `kpbr-n-<node id>`;
- a route rule from that inbound to that outbound;
- a fallback outbound `kpbr-direct` when the file had no outbounds of its own.

`dns`, `log`, `experimental` and every other inbound, outbound and route rule are copied through unchanged.

If the interface name is already an unmanaged inbound (for example a hand-written `sbtun`), the save is refused and the file is left as it was.

An older `sing-box-links.json` that still contains profiles is read and converted into separate nodes. Links live in `/opt/etc/keen-pbr/sing-box-links.json` and are not part of the API profile list.
