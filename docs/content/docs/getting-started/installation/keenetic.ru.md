---
title: Keenetic / NetCraze
weight: 1
---

На Keenetic и NetCraze keen-pbr-sx ставится из {{< term "Entware" >}}. Entware — набор дополнительных программ для роутера, который устанавливается на USB-накопитель. Ставьте его именно на USB: так установка пакетов и работа программ не изнашивают встроенную память роутера. Программы из Entware ставят командой `opkg` по SSH.

{{% steps %}}

### Установите Entware

Если Entware на роутере уже есть, переходите к следующему шагу.

Инструкция по установке есть в базе знаний [Keenetic](https://support.keenetic.com/?lang=ru) или [NetCraze](https://support.netcraze.ru/?lang=ru):

1. Найдите на сайте страницу своей модели роутера.
2. Введите в поиске «Установка репозитория Entware на USB-накопитель».
3. Выполните инструкцию по шагам. В ней же описано, как подключиться к роутеру по SSH: это понадобится дальше.

### Установите компоненты KeeneticOS

Откройте в браузере страницу компонентов роутера: `http://my.keenetic.net/system/osAndUpdates` (или `http://<IP-роутера>/system/osAndUpdates`). Установите следующие компоненты:

- Сетевые функции / **Протокол IPv6**. В NDMS 5.0 и новее этого пункта нет: протокол уже встроен.
- Пакеты OPKG / **Поддержка открытых пакетов**
- Пакеты OPKG / **Модули ядра подсистемы Netfilter**
- Пакеты OPKG / **Пакет расширений Xtables-addons для Netfilter**
- Утилиты и сервисы / **Прокси-сервер DNS-over-TLS** (необязательно, но рекомендуется)
- Утилиты и сервисы / **Прокси-сервер DNS-over-HTTPS** (необязательно, но рекомендуется)

### Включите шифрование DNS (рекомендуется)

{{< term "DoH" >}} (DNS-over-HTTPS) и DoT (DNS-over-TLS) — способы отправлять DNS-запросы в зашифрованном виде. Провайдер не сможет подменить ответы, и сайты будут открываться по правильным адресам.

Как настроить: [Прокси DoH и DoT для шифрования DNS-запросов](https://support.netcraze.ru/ultra/nc-1812/ru/31543.html).

### Установите keen-pbr-sx

1. Определите архитектуру Entware: `opkg print-architecture`. Выберите соответствующий каталог фида из списка ниже.
2. Подключите фид форка в конфигурации opkg, затем обновите список пакетов:

   ```sh
   echo 'src/gz keen-pbr-sx https://shendr404.github.io/keen-pbr-sx/keenetic/mipsel-3.4/' >> /opt/etc/opkg.conf
   opkg update
   opkg install keen-pbr-sx
   # Без веб-интерфейса: opkg install keen-pbr-sx-headless
   ```

   В примере используется `mipsel-3.4`; **замените** её на свою архитектуру и версию Entware: `mipsel-3.4`, `mips-3.4`, `aarch64-3.10`, `armv7-3.2` или `x64-3.2`.
3. Альтернатива: скачайте нужный `.ipk` со [страницы релизов](https://github.com/shendr404/keen-pbr-sx/releases) и установите локально через `opkg install ./имя_файла.ipk`.

### Обновление с keen-pbr 3.4.1

Пакет `keen-pbr-sx` конфликтует с `keen-pbr`. opkg в Entware сам конфликтующий пакет не удаляет, поэтому установка — две команды. `opkg remove` оставляет файлы конфигурации в `/opt/etc/keen-pbr`. Не удаляйте этот каталог до установки нового пакета.

```sh
opkg remove keen-pbr
opkg install keen-pbr-sx_<версия>_keenetic_<архитектура>.ipk
```

Для обычной сборки Keenetic файл называется `keen-pbr-sx_3.4.1-2_keenetic_mipsel-3.4.ipk`. Архитектуру смотрите в выводе `opkg print-architecture`. Headless ставится так же: сначала `opkg remove keen-pbr-headless`, затем `opkg install keen-pbr-sx-headless_*.ipk`.

Новый пакет копирует `config.json`, хранилище ссылок sing-box и профили sing-box в `/opt/etc/keen-pbr-sx`, если там ещё нет вашего конфига. Пути внутри конфига переписываются, старый `/opt/etc/init.d/S80keen-pbr` останавливается и отключается, старый блок `conf-script` в dnsmasq удаляется, чтобы остался новый хук. Повторная установка конфиг заново не копирует. dnsmasq и keen-pbr-sx запускаются вне сессии opkg.

`local.lst` и `dnsmasq-fallback.conf` не копируются. Если вы их меняли, скопируйте в `/opt/etc/keen-pbr-sx/` после установки. Значение `KEEN_PBR_RAW_PREROUTING` в `defaults` сохраняется, если оно не `auto`.

В конце установки появятся строки `Installation complete!` и адрес веб-интерфейса.

{{< callout type="info" >}}
`keen-pbr-sx-headless` занимает меньше места: около 1,2 МБ вместо 2,8 МБ. Веб-интерфейса и API в нём нет, настраивать придётся через файл конфигурации. Если сомневаетесь, ставьте обычный `keen-pbr-sx`.
{{< /callout >}}

### Проверьте, что сервис запущен

Выполните по SSH:

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr-sx status
```

Если всё в порядке, вы увидите `alive`:

```text
~ # /opt/etc/init.d/S80keen-pbr-sx status
 Checking keen-pbr-sx...              alive.
```

Если вместо `alive` написано `dead`, откройте раздел [Сервис не запускается]({{< relref "/docs/troubleshooting#service-does-not-start" >}}).

### Откройте веб-интерфейс

Откройте в браузере http://my.keenetic.net:12121. Если адрес не открывается, введите `http://<ip-роутера>:12121`, например `http://192.168.1.1:12121`.

В headless-версии веб-интерфейса нет. Конфигурация лежит в файле `/opt/etc/keen-pbr-sx/config.json`.

### Настройте первые правила

Перейдите к [Быстрому старту]({{< relref "/docs/getting-started/quick-start" >}}) и откройте вкладку **Веб-интерфейс**. Если поставили `keen-pbr-sx-headless`, откройте вкладку **JSON**.

{{% /steps %}}

## Команды для управления сервисом

| Действие | Команда |
| --- | --- |
| Запустить | `/opt/etc/init.d/S80keen-pbr-sx start` |
| Перезапустить | `/opt/etc/init.d/S80keen-pbr-sx restart` |
| Проверить, что работает | `/opt/etc/init.d/S80keen-pbr-sx status` |

## RAW или mangle

Для начала здесь ничего менять не нужно: keen-pbr-sx сам выберет подходящий режим. Раздел пригодится, если вы назначаете устройствам политики в разделе Keenetic **Приоритеты подключений** или если соединения через VPN иногда рвутся.

keen-pbr-sx помечает пакеты, которые должны уйти в VPN или другой канал. Такая метка называется {{< term "fwmark" >}}. Ставить её можно в одной из двух таблиц firewall роутера: RAW или {{< term "mangle" >}}.

| Режим | Плюс | Минус |
|---|---|---|
| **RAW** (по умолчанию, если роутер его поддерживает) | Keenetic эту таблицу не пересобирает, поэтому правила keen-pbr-sx работают без перерывов. Игровые сессии и другие долгие соединения не рвутся, трафик не уходит мимо VPN. | Политики из раздела **Приоритеты подключений** > **Применение политик** срабатывают позже и перезаписывают метку keen-pbr-sx. Поэтому RAW работает только для устройств с **Политикой по умолчанию**. |
| **mangle** | keen-pbr-sx срабатывает после политик Keenetic и может менять маршрут для устройств с любой политикой. | Keenetic пересобирает таблицу `mangle` (примерно раз в пять минут, а также, например, при продлении аренды DHCP). На это время правила keen-pbr-sx пропадают. Несколько пакетов могут уйти по обычному маршруту, и чувствительное соединение, например игровая сессия, оборвётся. |

Подробнее о том, как Keenetic пересобирает iptables, — в [обсуждении на форуме Keenetic Community](https://forum.keenetic.ru/topic/20631-keenetic-ndms-%D0%BE%D1%87%D0%B8%D1%89%D0%B0%D0%B5%D1%82-iptables-%D0%B2%D0%BE-%D0%B2%D1%80%D0%B5%D0%BC%D1%8F-%D0%B7%D0%B0%D0%BF%D1%83%D1%81%D0%BA%D0%B0-%D0%BF%D1%80%D0%BE%D0%B3%D1%80%D0%B0%D0%BC%D0%BC%D1%8B/).

{{< callout type="info" >}}
Проще всего не назначать устройствам политики в разделе **Приоритеты подключений**. Вместо этого создайте в keen-pbr-sx [правило маршрутизации]({{< relref "/docs/configuration/route-rules" >}}) и укажите в поле **Исходный IP** адрес устройства. Тогда все правила будут в одном месте и не станут конфликтовать с политиками Keenetic.
{{< /callout >}}

Режим выбирается отдельно для IPv4 и IPv6. Какой выбран, видно в системном журнале роутера после запуска сервиса:

```text
IPv4 raw PREROUTING available
WARNING: IPv6 raw PREROUTING unavailable: ...
```

Чтобы всегда использовать mangle, добавьте в файл `/opt/etc/keen-pbr-sx/defaults` строку:

```sh
KEEN_PBR_RAW_PREROUTING="disable"
```

и перезапустите сервис:

```sh
/opt/etc/init.d/S80keen-pbr-sx restart
```

Другие значения этой настройки описаны в разделе [CLI]({{< relref "/docs/cli#--use-raw-prerouting" >}}).

## Отдельные DNS-серверы для списков (dnsmasq) {#dnsmasq}

Для маршрутизации dnsmasq не нужен. dnsmasq — DNS-сервер, который ставится на роутер рядом с keen-pbr-sx. Ставьте его, если хотите отправлять домены некоторых списков на отдельные DNS-серверы или задать свои основные DNS-серверы. Подробнее: [Нужен ли dnsmasq?]({{< relref "/docs/configuration/dns#do-i-need-dnsmasq" >}}).

### Установка dnsmasq {#dnsmasq-install}

1. Установите пакет по SSH. В Entware есть только `dnsmasq-full`, имя `dnsmasq` указывает на него:
   ```bash {filename="bash"}
   opkg install dnsmasq-full
   ```
2. Примените рекомендуемые настройки dnsmasq:
   ```bash {filename="bash"}
   /opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh install-recommended-config
   ```
   Команда заменит `/opt/etc/dnsmasq.conf`. Прежний файл сохранится как `/opt/etc/dnsmasq.conf.backup-keenpbr`. Адрес `my.keenetic.net` будет открываться, как раньше.
3. Откройте веб-консоль роутера http://my.keenetic.net/a (или `http://<ip-роутера>/a`) и выполните:
   ```
   opkg dns-override
   system configuration save
   ```
   Выполняйте эти команды только после установки dnsmasq. Без dnsmasq `dns-override` оставит локальную сеть без DNS, и сайты перестанут открываться.
4. Перезагрузите роутер. **Это обязательно:** без перезагрузки устройства продолжат обращаться к встроенному DNS роутера, а не к dnsmasq.
5. В веб-интерфейсе keen-pbr-sx откройте **Общее** > **Настройки** > **Управление локальным DNS-сервером** и включите **Управлять конфигурацией dnsmasq**. Станут доступны страницы DNS-серверов и DNS-правил.

`opkg upgrade` и `opkg install --force-reinstall keen-pbr-sx` не отключают dnsmasq и не оставляют блок запасных серверов. Это делает только `opkg remove`. После установки или переустановки пакет возвращает управляемый блок, перезапускает dnsmasq, если он работал или был включён, и запускает keen-pbr-sx вне сессии opkg, чтобы установка его не остановила.

Если dnsmasq пишет `bind 172.19.0.1:53: Address already in use`, порт 53 всё ещё занимает ndnproxy, потому что `opkg dns-override` выключен. keen-pbr-sx эту настройку сам не меняет. В веб-консоли роутера (http://my.keenetic.net/a) выполните `opkg dns-override` и `system configuration save`, затем перезапустите dnsmasq.

### Вернуть как было {#dnsmasq-rollback}

Выполняйте шаги по порядку, тогда DNS в локальной сети не пропадёт ни на минуту:

1. В веб-интерфейсе keen-pbr-sx выключите **Управлять конфигурацией dnsmasq** (**Общее** > **Настройки** > **Управление локальным DNS-сервером**) и примените конфигурацию. dnsmasq продолжит работать на DNS-серверах из файла `/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf`. Если в настройках dnsmasq уже есть свои серверы, серверы из этого файла добавятся к ним. Чтобы этого не было, очистите файл.
2. В веб-консоли роутера (http://my.keenetic.net/a) выполните:
   ```
   no opkg dns-override
   system configuration save
   ```
3. Перезагрузите роутер. **Это обязательно.**
4. Удалите пакет:
   ```bash {filename="bash"}
   opkg remove dnsmasq-full
   ```

Если хотите оставить dnsmasq с настройками, которые были до `install-recommended-config`, не удаляйте пакет, а верните сохранённый файл:

```bash {filename="bash"}
cp /opt/etc/dnsmasq.conf.backup-keenpbr /opt/etc/dnsmasq.conf
/opt/etc/init.d/S56dnsmasq restart
```
