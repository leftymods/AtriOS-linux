<div align="center">

# MainlineAtriOS

**Фреймворк для сборки Linux-прошивок умных колонок**

Профили устройств, Mainline Linux и U-Boot, драйверы оборудования, системные службы и OTA.

[![Mainline checks](https://github.com/leftymods/AtriOS-linux/actions/workflows/mainline-checks.yml/badge.svg?branch=mainline)](https://github.com/leftymods/AtriOS-linux/actions/workflows/mainline-checks.yml?query=branch%3Amainline)
[![Signed OTA](https://github.com/leftymods/AtriOS-linux/actions/workflows/release-ota-runtime.yml/badge.svg?branch=mainline)](https://github.com/leftymods/AtriOS-linux/actions/workflows/release-ota-runtime.yml?query=branch%3Amainline)

[Устройства](DEVICES.md) · [Драйверы](DRIVERS.md) · [Релизы](https://github.com/leftymods/AtriOS-linux/releases) · [История изменений](CHANGELOG.md)

</div>

---

## Что это

MainlineAtriOS — framework для переноса и сборки Linux-систем под колонки. Профиль платы связывает конфигурацию ядра, Device Tree, загрузчик, драйверы и rootfs; общие скрипты собирают из них пакеты и образы, а CI проверяет релизные артефакты.

Проект рассчитан на расширение: новые платы добавляются отдельными профилями, а сборки ядра и загрузчика изолированы по устройствам. Сейчас единственное устройство с полной поддержкой — AtriStation Max.

## Устройства

| Профиль | Устройство и платформа | Статус |
|---|---|---|
| `station-max` (`yndxsttion53`) | AtriStation — софт-модификация Яндекс Станции Макс **YNDX-0053 MAX**; Amlogic **S905X3 (`sm1`) вместо S905X2** | Полная поддержка |
| `template` | Шаблон профиля для портирования новой колонки; SoC задаётся при настройке | Шаблон, аппаратная поддержка не заявлена |

Это список устройств и статусов из [DEVICES.md](DEVICES.md). Наличие шаблона или конфигурации сборки само по себе не означает, что плата проверена.

## Драйверы

Следующие драйверы отмечены как работающие для `station-max` в [DRIVERS.md](DRIVERS.md):

| Область | Работающие драйверы |
|---|---|
| LED и дисплей | `gowin_led_device`, `quasar_led_screen`, `leds-is31fl32xx`, `quasar_led_ring`, `meson-drm` |
| Аудио и микрофоны | `snd-soc-sy6045s`, `snd-soc-es8156`, `snd-soc-es7210`, `snd-soc-dmic`, `axg-sound-card` |
| Управление и датчики | `quasar_pcba_ids`, `rotary-volume`, `gpio_keypad`, `zigbee-control`, `ltr308als01` |
| Беспроводная связь | `rtw88_8822cs`, `hci_uart`, `btrtl` |

На этой ревизии не используются `snd-soc-tas5825m` и `snd-soc-pcm1774`: соответствующих микросхем на плате нет. Подробности, интерфейсы и причины указаны в [DRIVERS.md](DRIVERS.md).

## Что даёт framework

- **Профили плат** — отдельные параметры SoC, DTS, ядра, загрузчика, rootfs и serial console.
- **Переносимость сборки** — отдельные профили и кэши ядра, зафиксированные версии исходников U-Boot и FIP.
- **Системная интеграция** — overlay-файлы rootfs, драйверы и службы на C и Rust.
- **Обновления** — подписанные OTA-артефакты, проверка целостности и поддержка отката.
- **Проверки** — unit-тесты и CI для профилей, сценариев сборки и релизов.

## Быстрый старт

Команды выполняются из корня репозитория в Linux или WSL.

```bash
# Список профилей
./compile.sh list

# Конфигурация поддерживаемой AtriStation
./compile.sh info BOARD=station-max

# Полный образ
./compile.sh BOARD=station-max RELEASE=Nebula TARGET=image
```

Можно собирать отдельные цели: `TARGET=kernel`, `TARGET=uboot`, `TARGET=rootfs` и `TARGET=packages`.

Проверки репозитория:

```bash
make test
make verify
```

## Структура репозитория

| Каталог | Назначение |
|---|---|
| `config/boards/` | Профили и параметры устройств |
| `config/bootloaders/` | Рецепты сборки загрузчиков |
| `board/` | Device Tree, конфигурации ядра, драйверы и rootfs overlays |
| `lib/`, `scripts/` | Сборка ядра, rootfs, пакетов и образов |
| `tests/` | Регрессионные и профильные тесты |
| `.github/workflows/` | CI и публикация прошивок и OTA |

## Документация

- [Устройства и добавление профиля новой платы](DEVICES.md)
- [Матрица драйверов и их статусы](DRIVERS.md)
- [История версий](CHANGELOG.md)
- [Готовые релизы](https://github.com/leftymods/AtriOS-linux/releases)
