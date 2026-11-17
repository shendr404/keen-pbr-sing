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

## Notes for review

No firewall rules were added, so the packet path is unchanged. The iptables aggregation is one extra pass over rules we already parse.

The README section that names this tree and the `upstream` remote is documentation for the fork. Drop that section if you take only the counter change. The install notes for an Entware ipk over an existing `/opt/etc/keen-pbr/config.json` can stay: the file is already a conffile, and the steps tell the operator not to pass `--force-maintainer`.
