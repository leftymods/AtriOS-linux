# AtriOS Nebula

**AtriOS Nebula** — модульный фреймворк и дистрибутив на базе **Mainline Linux (`AtriOS-aml`)** и **Mainline U-Boot** для умных колонок и аудиоплатформ на базе ARM64/Amlogic (от **leftymods**).

Фреймворк рассчитан на поддержку **разных моделей колонок** через систему профилей сборки (`config/boards/*.conf`, `config/families/*.conf`) и динамическое автоопределение железа в рантайме (`/usr/lib/atrios/device-detect` по Device Tree `compatible`).

---

## Поддерживаемые и доступные устройства (`config/boards/`)

| Board ID | Устройство | Семейство / SoC | Конфиг сборки | Статус |
|---|---|---|---|---|
| `station-max` | **AtriStation Max (`nebula`)** / Yandex Station Max | `amlogic-meson64` / Amlogic S905X3 (`sm1`) | `config/boards/station-max.conf` | Полная поддержка (основная платформа) |
| `yndxsttion53` | Yandex Station Max (`yndxsttion53`) | `amlogic-meson64` / Amlogic S905X3 (`sm1`) | `config/boards/yndxsttion53.conf` | Профиль сборки |
| `template` | Шаблон для новой колонки (`new-board`) | Настраивается (`amlogic-meson64` и др.) | `config/boards/template.conf` | Шаблон расширения |

### Добавление новой колонки во фреймворк
1. Скопируйте `config/boards/template.conf` в `config/boards/<board>.conf` и задайте `BOARD`, `SOC`, `DTS_NAME`, `KERNEL_DEFCONFIG`.
2. Добавьте ветку `case` с `compatible` вашей платы в `/usr/lib/atrios/device-detect` (для `fastfetch`, `S00resizefs` и OTA-проверок).
3. Соберите образ через `./compile.sh` (или `./atrios`).

---

## Идентификация платформы

* **ОС / дистрибутив:** `AtriOS Nebula` (`hostname`: `AtriOS`)
* **Ядро Linux:** `AtriOS-aml` (`CONFIG_LOCALVERSION="-AtriOS-aml"`)
* **Профиль устройства:** определяется автоматически по Device Tree (`compatible = "leftymods,station-max", "amlogic,sm1"`) через `/usr/lib/atrios/device-detect` и может переопределяться в `/etc/atrios-device`.

---

## Архитектура

### 1. Mainline U-Boot
* **Конфигурация:** `board/yandex/station2/mainline/uboot_station_max_defconfig`
* **Device Tree:** `board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max-u-boot.dtsi`
* **Скрипт сборки и FIP-подписи:** `scripts/build_mainline_uboot.sh`
* **Режим прошивки:** USB OTG (`dr_mode = "otg"`), `fastboot` и `aml_burn`.

### 2. Mainline Linux Kernel (`AtriOS-aml`)
* **Device Tree:** `board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts`
* **Defconfig ядра:** `board/yandex/station2/mainline/linux_7.0_station_max_defconfig`

### 3. Userspace и сервисы (`rootfs_overlay`)
* **Init-система:** `S00resizefs` (авторасширение корневого раздела на eMMC), `S01modules`, `S02baseline` (снимок базовых пакетов и дефолтных настроек), `S03amplifiers`, `S40network`, `S50pipewire`, `S60atrios`, `S71atrios-bt-control`.
* **Аудио и беспроводной стек:** PipeWire + WirePlumber, BlueZ 5 + BlueALSA, `bt-speaker-daemon`, `wpa_supplicant`, ALSA.
* **Системная диагностика и профили:** `/usr/bin/fastfetch` (янтарное лого AtriOS), `/usr/lib/atrios/device-detect`.

### 4. Драйверы ядра (`drivers/atrios/`)

| Модуль | Назначение | Интерфейс |
|---|---|---|
| `gowin_led_device.ko` | Контроллер LED-матрицы FPGA Gowin GW1N-9 (SPI + JTAG/MMIO Flash) | SPI + GPIO / MMIO |
| `quasar_led_screen.ko` | Протокол кадрового буфера LED-матрицы 25×16 | SPI / fbdev |
| `quasar_led_ring.ko` | Верхнее кольцо из 24 RGB-светодиодов | LED trigger / fbdev |
| `quasar_pcba_ids.ko` | Чтение заводских идентификаторов плат из EEPROM в формате SV2/SV1 | I2C (`24c02`) |
| `rotary-volume.ko` | Энкодер регулятора громкости | GPIO interrupts |
| `gpio_keypad.ko` | Аппаратные кнопки управления | Polled GPIO |
| `zigbee-control.ko` | Управление сбросом и bootloader модуля Zigbee EFR32 | GPIO |
| `ltr308als01.ko` | Датчик внешней освещённости | I2C / IIO |
| `leds-is31fl32xx.ko` | Драйвер LED-контроллера IS31FL3236A | I2C |
| `snd-soc-sy6045s.ko` | Усилитель НЧ/ВЧ с DSP biquad-фильтрами | I2C / I2S |
| `snd-soc-es8156.ko` | Аудио ЦАП линейного выхода / наушников | I2C / I2S |
| `snd-soc-es7210.ko` | 4-канальный АЦП микрофонного массива | I2C / TDM |
| `snd-soc-tas5825m.ko` | Усилитель TAS5825M | I2C / I2S |

---

## Аппаратные интерфейсы (AtriStation Max / Station Max)

* **Фреймбуферы:**
  * `/dev/fb0` — HDMI (`mesondrmfb`).
  * `/dev/fb1` (симлинк `/dev/fb_screen`) — фронтальный монохромный LED-экран Gowin 25×16 (`YNDX LED screen`). Демон `screend` выводит часы и анимации.
  * `/dev/fb2` (симлинк `/dev/fb_ring`) — верхнее кольцо из 24 RGB-светодиодов (`quasar-led-ring` на IS31FL3236A). Демон `rotaryd` отображает уровень громкости.
* **Беспроводные модули (Realtek RTL8822CS + EFR32):**
  * **Wi-Fi:** SDIO (`sd_emmc_a`), драйвер `rtw88_8822cs`, 2.4 / 5 ГГц.
  * **Bluetooth 5.0:** UART Serdev (`/dev/ttyAML1`), A2DP Sink + AVRCP + BLE.
  * **Zigbee (Silicon Labs EFR32 MG21):** UART (`/dev/ttyAML2`), управление сбросом через `zigbee-control`.
* **Аудиосистема (`AML-AUGESOUND`):**
  * Усилители НЧ/ВЧ SY6045S (`2-002b`, `2-002a`) с калибровочными DSP biquad/DRC-профилями.
  * ЦАП наушников/AUX ES8156 (`2-0008`), АЦП ES7210 (`2-0040`) и 8-канальный PDM-массив микрофонов (`dmics`).

---

## Единая утилита `atrios`

Все операции управления системой, периферией, обновлениями и сбросом собраны в `/usr/bin/atrios`:

```bash
# Общий статус и система
atrios status                           # Обзор Wi-Fi, Bluetooth и Zigbee
atrios info                             # Системная сводка (fastfetch)
atrios check                            # Аудит аудиотракта и шин

# Экран 25x16, LED-кольцо и датчики
atrios screen clock|kisser|eyes|off     # Часы, анимации или отключение матрицы
atrios ring rainbow|volume N|off        # Подсветка верхнего кольца
atrios sensor                           # Показания датчика освещённости (lux)

# Аудио и беспроводные интерфейсы
atrios volume [0..100%]                 # Управление громкостью
atrios mute [on|off]                    # Отключение звука
atrios wifi [scan|connect SSID PASS]    # Управление Wi-Fi
atrios bt [init|scan]                   # Управление Bluetooth
atrios zigbee [reset|ping]              # Диагностика EFR32 Zigbee

# OTA-обновления и сброс к заводским настройкам
atrios ota status                       # Текущая версия, статус SWUpdate и бэкапов .bak
atrios ota install <pkg.tar.gz|.swu|URL> [--reboot]  # Проверка SHA-256 и установка OTA
atrios ota rollback [--reboot]          # Откат ядра и DTB из .bak на mmcblk1p1
atrios reset                            # Полный сброс колонки (двойное подтверждение)
```

---

## OTA-обновления и Factory Reset

### OTA / SWUpdate (`atrios ota`)
* Поддерживает пакеты `.tar.gz` с `manifest.json` (проверка `device_id` и контрольных сумм SHA-256 до начала записи) и образы `.swu` (через `swupdate`, если бинарник установлен), включая загрузку напрямую с сервера по URL (`http(s)://...`).
* Обновляет ядро `Image` и DTB одновременно в `/boot` и на загрузочном разделе `/dev/mmcblk1p1`, сохраняя резервные копии `.bak` для отката (`atrios ota rollback`), а также применяет rootfs-архивы и `.deb`-пакеты.

### Factory Reset (`atrios reset`)
* Требует двойного интерактивного подтверждения (`YES` → `RESET`; для автоматизации через API используется `--yes`).
* Сравнивает текущие `dpkg`-пакеты с эталоном `/etc/atrios-base-packages.list` и полностью удаляет все доустановленные пользователем пакеты (`apt-get purge` + `autoremove`), pip-пакеты, аккаунты с `UID >= 1000` и `/home/*`, учётные данные Wi-Fi, сопряжения Bluetooth, SSH/Dropbear-ключи, истории и состояние `/var/lib/atrios/*`.

---

## Сборка и верификация

```bash
# Проверка компиляции DTS, драйверов и userspace-скриптов
./scripts/verify_all.sh

# Сборка ядра и образа
./scripts/build_kernel_and_image.sh

# Сборка Mainline U-Boot
./scripts/build_mainline_uboot.sh
```

---

## Прошивка

### Способ 1: Amlogic USB Burning Tool
1. Откройте образ в **Amlogic USB Burning Tool** (`File -> Import image`) и нажмите **Start**.
2. Зажмите кнопку **Mute** на верхней панели и подключите питание 12V + USB-кабель к ПК (BootROM DFU / `WorldCup Device`).
3. После завершения нажмите **Stop**, отключите USB и перезагрузите станцию.

### Способ 2: UART + Fastboot
1. Подключите USB-UART (3.3V, `115200 8N1`), прервите загрузку в U-Boot и выполните:
   ```text
   fastboot 0
   ```
2. На ПК прошейте разделы и перезагрузите устройство:
   ```bash
   fastboot flash bootloader u-boot.bin
   fastboot flash boot boot.img
   fastboot flash rootfs rootfs.ext4
   fastboot reboot
   ```

---

## Авторство и лицензия
* **Разработчик:** leftymods `<ggalab33@gmail.com>`
* **Лицензия:** GPL-2.0-or-later / MIT
