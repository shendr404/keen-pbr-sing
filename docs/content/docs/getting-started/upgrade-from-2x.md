---
title: Upgrade from 2.x
weight: 3
---

This guide is for those who already run keen-pbr-sx 2.x on Keenetic / NetCraze. You will replace the repository and the package and move your configuration to the new format. Run all commands over SSH.

## 1. Make a backup

Save your current 2.x configuration. If something goes wrong, you can compare it with the converted result:

```bash {filename="bash"}
cp /opt/etc/keen-pbr-sx/keen-pbr-sx.conf /opt/etc/keen-pbr-sx/keen-pbr-sx.conf.bak
```

## 2. Replace the repository

Open the repository page, select the OS **Keenetic / NetCraze** on the left and your router architecture. Run the commands it shows, in order, to replace the old keen-pbr-sx 2.x repository with the current one:

{{< hextra/hero-button text="keen-pbr-sx repository" link="https://github.com/shendr404/keen-pbr-sx/releases" >}}

## 3. Install keen-pbr-sx

```bash {filename="bash"}
opkg install keen-pbr-sx

# version without the web UI and API:
# opkg install keen-pbr-sx-headless
```

The new configuration is in `/opt/etc/keen-pbr-sx/config.json`.

## 4. Convert the config to the new format

Open the [Config Converter](https://shendr404.github.io/keen-pbr-sx/converter/). Then:

1. Copy the content of `/opt/etc/keen-pbr-sx/keen-pbr-sx.conf` into the converter input field on the left.
2. Review the generated config.
3. Replace the content of `/opt/etc/keen-pbr-sx/config.json` with the converted output.

{{< callout type="info" >}}
Replace the config file only after you have reviewed the converted output.
{{< /callout >}}

## 5. Restart keen-pbr-sx

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr-sx restart
/opt/etc/init.d/S80keen-pbr-sx status
```

If everything is fine, `status` prints `alive`. If it says `dead`, open [Service does not start]({{< relref "/docs/troubleshooting#service-does-not-start" >}}).

Then open [Quick Start]({{< relref "/docs/getting-started/quick-start" >}}): the **Web UI** tab helps you check the setup, and the **JSON** tab is for working with the config file.
