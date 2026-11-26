---
title: Обновление с 2.x
weight: 3
---

Это руководство для тех, у кого на Keenetic / NetCraze уже работает keen-pbr-sx 2.x. Вы замените репозиторий и пакет и перенесёте конфигурацию в новый формат. Все команды выполняются по SSH.

## 1. Сделайте резервную копию

Сохраните текущую конфигурацию 2.x. Если что-то пойдёт не так, вы сможете сравнить её с результатом конвертации:

```bash {filename="bash"}
cp /opt/etc/keen-pbr-sx/keen-pbr-sx.conf /opt/etc/keen-pbr-sx/keen-pbr-sx.conf.bak
```

## 2. Замените репозиторий

Откройте страницу репозитория, слева выберите ОС **Keenetic / NetCraze** и архитектуру своего роутера. Выполните показанные команды по порядку, чтобы заменить старый репозиторий keen-pbr-sx 2.x на текущий:

{{< hextra/hero-button text="Репозиторий keen-pbr-sx" link="https://github.com/shendr404/keen-pbr-sx/releases" >}}

## 3. Установите keen-pbr-sx

```bash {filename="bash"}
opkg install keen-pbr-sx

# версия без веб-интерфейса и API:
# opkg install keen-pbr-sx-headless
```

Новая конфигурация лежит в `/opt/etc/keen-pbr-sx/config.json`.

## 4. Сконвертируйте конфигурацию в новый формат

Откройте [конвертер конфига keen-pbr-sx](https://shendr404.github.io/keen-pbr-sx/converter/). Затем:

1. Скопируйте содержимое `/opt/etc/keen-pbr-sx/keen-pbr-sx.conf` в поле ввода конвертера слева.
2. Проверьте сгенерированный конфиг.
3. Замените содержимое `/opt/etc/keen-pbr-sx/config.json` на результат конвертации.

{{< callout type="info" >}}
Заменяйте конфигурационный файл только после проверки содержимого.
{{< /callout >}}

## 5. Перезапустите keen-pbr-sx

```bash {filename="bash"}
/opt/etc/init.d/S80keen-pbr-sx restart
/opt/etc/init.d/S80keen-pbr-sx status
```

Если всё в порядке, `status` напишет `alive`. Если написано `dead`, откройте раздел [Сервис не запускается]({{< relref "/docs/troubleshooting#service-does-not-start" >}}).

Затем откройте [Быстрый старт]({{< relref "/docs/getting-started/quick-start" >}}): вкладка **Веб-интерфейс** поможет проверить настройку, вкладка **JSON** нужна, если вы работаете с файлом конфигурации.
