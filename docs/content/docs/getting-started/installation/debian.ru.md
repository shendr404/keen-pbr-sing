---
title: Debian
weight: 3
---

Этот форк не публикует APT-репозиторий Debian. Собранный пакет `.deb` можно установить локально от имени `root` (или через `sudo`).

{{% steps %}}

### Получите пакет

Соберите `.deb` по инструкции [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}).

### Установите пакет

```bash {filename="bash"}
apt install ./keen-pbr-sx_*.deb

# Для headless-варианта установите локальный keen-pbr-sx-headless_*.deb
```

Конфигурация лежит в `/etc/keen-pbr-sx/config.json`. Сервис запускается сразу и стартует при каждой загрузке системы.

{{< callout type="info" >}}
`keen-pbr-sx-headless` занимает меньше места: около 1,2 МБ вместо 2,8 МБ. Веб-интерфейса и API в нём нет, настраивать придётся через файл конфигурации. Если сомневаетесь, ставьте обычный `keen-pbr-sx`.
{{< /callout >}}

### Проверьте, что сервис запущен

```bash {filename="bash"}
systemctl status keen-pbr-sx
```

Если всё в порядке, в выводе будет `active (running)`.

### Откройте веб-интерфейс

Откройте в браузере `http://<ip-сервера>:12121`. В headless-версии веб-интерфейса нет.

### Настройте первые правила

Перейдите к [Быстрому старту]({{< relref "/docs/getting-started/quick-start" >}}) и откройте вкладку **Веб-интерфейс**. Если поставили `keen-pbr-sx-headless`, откройте вкладку **JSON**.

{{< callout type="info" >}}
Если готовых пакетов для вашей платформы нет, соберите keen-pbr-sx сами: [Сборка из исходного кода]({{< relref "/docs/developer/build-from-source" >}}).
{{< /callout >}}

{{% /steps %}}

## Команды для управления сервисом

```bash {filename="bash"}
systemctl start keen-pbr-sx
systemctl status keen-pbr-sx
systemctl restart keen-pbr-sx
```

## Отдельные DNS-серверы для списков (dnsmasq) {#dnsmasq}

Для маршрутизации dnsmasq не нужен. dnsmasq — DNS-сервер, который ставится на систему рядом с keen-pbr-sx. Ставьте его, если хотите отправлять домены некоторых списков на отдельные DNS-серверы или задать свои основные DNS-серверы. Подробнее: [Нужен ли dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Установка dnsmasq {#dnsmasq-install}

```bash {filename="bash"}
apt install dnsmasq
```

Чтобы применить рекомендуемые настройки dnsmasq, выполните:

```bash {filename="bash"}
/usr/lib/keen-pbr-sx/dnsmasq-hook.sh install-recommended-config
```

Команда заменит `/etc/dnsmasq.conf`, прежний файл сохранится как `/etc/dnsmasq.conf.backup-keenpbr`. После этого dnsmasq отвечает только вашим локальным сетям и берёт серверы у keen-pbr-sx.

Затем в веб-интерфейсе откройте **Общее** > **Настройки** > **Управление локальным DNS-сервером** и включите **Управлять конфигурацией dnsmasq**. Если система ещё не использует dnsmasq, направьте на него системный резолвер или DHCP-сервер.

### Вернуть как было {#dnsmasq-rollback}

1. Выключите **Управлять конфигурацией dnsmasq** и примените конфигурацию. dnsmasq перезапустится и будет использовать серверы из `/etc/keen-pbr-sx/dnsmasq-fallback.conf`. Если у dnsmasq есть свои серверы, эти добавятся к ним. Чтобы этого не было, очистите файл.
2. Верните системный резолвер, который был до dnsmasq.
3. Если запускали `install-recommended-config`, верните прежние настройки:
   ```bash {filename="bash"}
   cp /etc/dnsmasq.conf.backup-keenpbr /etc/dnsmasq.conf
   systemctl restart dnsmasq
   ```
4. Или удалите пакет:
   ```bash {filename="bash"}
   apt remove dnsmasq
   ```
