---
title: OpenWrt
weight: 2
---

Этот форк не публикует OpenWrt-фид. Для OpenWrt нужно собрать пакет из исходников и установить локально по SSH.

{{% steps %}}

### Узнайте, какой менеджер пакетов у вашей OpenWrt

Менеджер пакетов — программа, которая устанавливает и обновляет другие программы. Какой из них у вас, зависит от версии:

- OpenWrt 25.x и новее: `apk`
- OpenWrt 24.x и старше: `opkg`

### Установите keen-pbr-sx

1. Подключитесь к роутеру по SSH.
2. Соберите пакет нужной архитектуры по инструкции [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}).
3. Скопируйте пакет на роутер и установите локально (`opkg install ./файл.ipk` или `apk add --allow-untrusted ./файл.apk`). Следующие команды применимы только при наличии собственного фида:

```bash {filename="bash"}
# OpenWrt 25.x и новее
apk update
apk add keen-pbr-sx

# версия без веб-интерфейса и API:
# apk add keen-pbr-sx-headless
```

```bash {filename="bash"}
# OpenWrt 24.x и старше
opkg update
opkg install keen-pbr-sx

# версия без веб-интерфейса и API:
# opkg install keen-pbr-sx-headless
```

Конфигурация лежит в `/etc/keen-pbr-sx/config.json`. Сервис запускается сам при каждой загрузке роутера.

{{< callout type="info" >}}
`keen-pbr-sx-headless` занимает меньше места: около 1,2 МБ вместо 2,8 МБ. Веб-интерфейса и API в нём нет, настраивать придётся через файл конфигурации. Если сомневаетесь, ставьте обычный `keen-pbr-sx`.
{{< /callout >}}

{{< callout type="warning" >}}
Пакет сам ставит нужные модули ядра. Если в логе есть строка `cannot bind netfilter queue/log ... Invalid argument`, установите их вручную: `kmod-nfnetlink-queue`, `kmod-nfnetlink-log` и `kmod-nft-queue`.
{{< /callout >}}

### Проверьте, что сервис запущен

```bash {filename="bash"}
service keen-pbr-sx status
```

Если всё в порядке, команда напишет `running`.

### Откройте веб-интерфейс

Откройте в браузере `http://<ip-роутера>:12121`, например `http://192.168.1.1:12121`. В headless-версии веб-интерфейса нет.

### Настройте первые правила

Перейдите к [Быстрому старту]({{< relref "/docs/getting-started/quick-start" >}}) и откройте вкладку **Веб-интерфейс**. Если поставили `keen-pbr-sx-headless`, откройте вкладку **JSON**.

{{< callout type="info" >}}
Если готовых пакетов для вашей платформы нет, соберите keen-pbr-sx сами: [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}).
{{< /callout >}}

{{% /steps %}}

## Команды для управления сервисом

```bash {filename="bash"}
service keen-pbr-sx start
service keen-pbr-sx restart
service keen-pbr-sx status
service keen-pbr-sx enable
```

`enable` включает автозапуск при загрузке роутера.

## Отдельные DNS-серверы для списков (dnsmasq) {#dnsmasq}

В OpenWrt dnsmasq уже работает как DNS-сервер роутера. Для маршрутизации keen-pbr-sx он не нужен. Стоит ли отдать его под управление keen-pbr-sx, читайте в разделе [Нужен ли dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Включение {#dnsmasq-install}

В веб-интерфейсе откройте **Общее** > **Настройки** > **Управление локальным DNS-сервером** и включите **Управлять конфигурацией dnsmasq**.

Пока управление включено и заданы **Основные DNS-серверы**, keen-pbr-sx убирает из списка в `/etc/config/dhcp` (в LuCI **DNS forwardings**) серверы без домена. Серверы для отдельных доменов, например `/lan/192.168.1.1`, остаются. Убранные серверы вернутся, когда вы выключите управление или очистите эти серверы.

### Вернуть как было {#dnsmasq-rollback}

Выключите **Управлять конфигурацией dnsmasq** и примените конфигурацию. keen-pbr-sx перезапустит dnsmasq и вернёт убранные серверы.
