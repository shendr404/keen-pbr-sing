# Per-outbound traffic counters

## Summary

I want to see whether traffic is actually flowing through each outbound, especially an `interface` outbound whose device is a sing-box TUN (`sbtun`). The Web UI, `GET /api/runtime/outbound-traffic` and `/metrics` now show, for every configured outbound:

- cumulative bytes and packets sent and received, where the source can tell the directions apart
- the current rate, as bytes/s since the previous sample (a few seconds)
- the number of conntrack entries marked with that outbound's fwmark, when the kernel exposes marks

## Where the numbers come from

I checked the iptables path before picking a source. Classification uses `MARK` in mangle, then `CONNMARK --save-mark`. The next packet in the original direction is restored and returned before it reaches the `MARK` rule (`prefilter.restore_conntrack_mark` runs ahead of the route rules, and reply packets are returned by `prefilter.skip_local_replies`). So the existing `iptables-save -c` counters on those `MARK` rules are classified packets, not the full flow. They also reset whenever an apply rebuilds the rules.

`/proc/net/dev` is cumulative for the life of the interface and needs nothing but a read. On a TUN, tx is what the kernel sends into the device and rx is what it receives back. That is the right signal for `sbtun` on Keenetic (kernel 4.9, iptables, Entware mipsel-3.4). Interface outbounds use it.

Other outbounds do not have a device of their own (a table outbound shares the WAN). For those I sum the keen-pbr `MARK` rules whose `--set-xmark` value is that outbound's fwmark, using the parser that `/metrics` already uses for balance counters. Foreign rules with the same mark are ignored because they have no `kpbr:v1:` comment. Receive counters are left out, and the UI labels the numbers as classified traffic. nftables still has no per-rule counters, so a non-interface outbound stays `source: none` there; an interface outbound still uses `/proc/net/dev`.

Active connections are a scan of `/proc/net/nf_conntrack` for `mark=<fwmark>`. `secmark=` is not treated as `mark=`. If the file is missing or has no mark field, `connections` is omitted instead of reported as zero. The sample is cached for two seconds so a UI poll and a Prometheus scrape do not both walk conntrack.

## API and UI

`GET /api/runtime/outbound-traffic` returns one entry per configured outbound (`source` is `interface`, `fwmark` or `none`). It is polled; it is not part of the status stream, because the rate has to move even when nothing else changes.

`/metrics` gains:

- `keen_pbr_outbound_rx_bytes_total` / `keen_pbr_outbound_tx_bytes_total`
- `keen_pbr_outbound_rx_packets_total` / `keen_pbr_outbound_tx_packets_total`
- `keen_pbr_outbound_rx_bytes_per_second` / `keen_pbr_outbound_tx_bytes_per_second`
- `keen_pbr_outbound_connections`

Labels are `outbound`, `type`, `source` and `interface`. A direction the source cannot see is omitted, not emitted as zero.

The Outbounds page has a Traffic column. The overview cards show the same rate (or the cumulative totals until the second sample).

## Keenetic follow-ups

These showed up on a Keenetic with Entware, the iptables backend, NDM `FORWARD` policy DROP, and a sing-box TUN named `sbtun`:

- Interface outbounds get `FORWARD` accepts (LAN to the interface, and the return path `RELATED,ESTABLISHED`). They are inserted only when missing. `/opt/etc/ndm/netfilter.d/51-keen-pbr-forward.sh` installs them again after NDM rebuilds the filter table. `ip6tables` runs only when IPv6 is enabled. Stop and uninstall remove the rules.
- A TUN, or a point-to-point interface with no gateway route, is reachable as `default dev <iface>`. `gateway: "auto"` is ignored and the UI says so, instead of leaving the outbound unavailable.
- If the managed block in `/opt/etc/dnsmasq.conf` loses `conf-script=... dnsmasq-config-entry`, apply puts the line back, keeps the previous file as `dnsmasq.conf.backup-keenpbr-confscript`, and the dnsmasq status warnings report it.
- A downloaded list that is an HTML page or has a line over 4096 bytes is rejected. The previous cache stays. A cache that is already in that state is left unused instead of aborting the list with "List line exceeds 4096-byte limit".
- README describes a DNS server `detour` plus a `dns.rules` entry for lists whose names the ISP poisons. The config check warns when a list is routed through an interface, urltest, or icmptest outbound and no such rule exists.

## Notes for review

No firewall rules were added, so the packet path is unchanged. The iptables aggregation is one extra pass over rules we already parse.

The README section that names this tree and the `upstream` remote is documentation for the fork. Drop that section if you take only the counter change. The install notes for an Entware ipk over an existing `/opt/etc/keen-pbr/config.json` can stay: the file is already a conffile, and the steps tell the operator not to pass `--force-maintainer`.
