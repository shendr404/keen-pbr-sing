---
title: Sing-box connections and subscriptions
weight: 7
---

keen-pbr-sing integrates sing-box TUN routing with Keenetic policy routing.

Use the **Sing-box** Web UI page to preview and add `vless://`, `hysteria2://`, `trojan://`, `ss://` or `vmess://` connections. Configure an `interface` outbound for `sbtun` with gateway `auto`.

Subscription sources can contain plain or base64-encoded links, sing-box JSON, or Clash JSON. Set a refresh interval in hours; `0` disables automatic refresh. The daemon refreshes due subscriptions in the background, limiting downloads to 2 MiB with a 20-second timeout and retrying failures after five minutes.

To manually refresh:

```sh
/opt/usr/bin/keen-pbr subscriptions refresh --all
/opt/usr/bin/keen-pbr subscriptions refresh <id>
```

Before applying changed nodes, the existing sing-box config is backed up and checked using `sing-box check`. Invalid changes are rolled back. Links remain private in `/opt/etc/keen-pbr/sing-box-links.json`.
