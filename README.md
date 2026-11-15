# AtriOS Nebula

**AtriOS Nebula** — модульный фреймворк и дистрибутив на базе **Mainline Linux (`AtriOS-aml`)** и **Mainline U-Boot** для умных колонок и встраиваемых аудиоплатформ от **leftymods**.

Фреймворк объединяет профили сборки разных устройств (`config/boards/*.conf`, `config/families/*.conf`), набор перенесённых на mainline-ядро драйверов (`drivers/atrios/`), единую CLI-утилиту `atrios`, автоопределение железа в рантайме (`/usr/lib/atrios/device-detect`), OTA-обновления и полный сброс до заводского состояния.

---

## Поддерживаемые устройства (`config/boards/`)

| Board ID | Платформа | Семейство / SoC | Конфиг сборки | Статус |
|---|---|---|---|---|
| `station-max` | **AtriStation Max (`nebula`)** / Yandex Station Max | `amlogic-meson64` / Amlogic S905X3 (`sm1`) | `config/boards/station-max.conf` | Полная поддержка |
| `yndxsttion53` | Yandex Station Max (`yndxsttion53`) | `amlogic-meson64` / Amlogic S905X3 (`sm1`) | `config/boards/yndxsttion53.conf` | Профиль сборки |
| `template` | Шаблон для новой колонки (`new-board`) | Настраивается (`amlogic-meson64` и др.) | `config/boards/template.conf` | Шаблон расширения |

### Добавление новой колонки во фреймворк
1. Скопируйте `config/boards/template.conf` в `config/boards/<board>.conf` и задайте параметры `BOARD`, `VENDOR`, `BOARDFAMILY`, `SOC`, `DTS_NAME`, `UBOOT_DEFCONFIG`, `KERNEL_DEFCONFIG`, `ROOTFS_OVERLAY`.
2. Разместите Device Tree, defconfig и overlay в `board/<vendor>/<device>/`.
3. Добавьте профиль платы (по `compatible` из Device Tree) в `/usr/lib/atrios/device-detect` (для `fastfetch`, `S00resizefs` и проверки совместимости при OTA).

---

## Общая архитектура фреймворка

### 1. Конфигурация и сборка (`config/`, `scripts/`)
* **Профили семейств и плат:** `config/families/*.conf` и `config/boards/*.conf` задают архитектуру, SoC, загрузчик, DTS и rootfs overlay для каждого устройства.
* **Mainline U-Boot:** скрипт сборки и FIP-подписи `scripts/build_mainline_uboot.sh` с поддержкой USB OTG, `fastboot` и `aml_burn`.
* **Mainline Linux (`AtriOS-aml`):** единое дерево ядра Linux 7.x с внешним набором драйверов `drivers/atrios/`.

### 2. Userspace и сервисы (`rootfs_overlay`)
* **Init-система:** `S00resizefs` (авторасширение rootfs с учётом профиля платы), `S01modules`, `S02baseline` (снимок базовых пакетов и шаблонов), `S03amplifiers`, `S40network`, `S50pipewire`, `S60atrios`, `S71atrios-bt-control`.
* **Аудио и беспроводной стек:** PipeWire + WirePlumber, BlueZ 5 + BlueALSA, `bt-speaker-daemon`, `wpa_supplicant`, ALSA.
* **Автоопределение и диагностика:** `/usr/lib/atrios/device-detect`, `/usr/bin/fastfetch` (динамически считывает SoC, фреймбуферы и звуковые карты).

### 3. Библиотека драйверов (`drivers/atrios/`)

| Модуль | Назначение | Интерфейс |
|---|---|---|
| `gowin_led_device.ko` | Контроллер LED-матрицы FPGA Gowin GW1N-9 (SPI + JTAG/MMIO Flash) | SPI + GPIO / MMIO |
| `quasar_led_screen.ko` | Драйвер кадрового буфера LED-матрицы | SPI / fbdev |
| `quasar_led_ring.ko` | Верхнее RGB LED-кольцо | LED trigger / fbdev |
| `quasar_pcba_ids.ko` | Чтение заводских идентификаторов плат из EEPROM (SV2/SV1) | I2C (`24c02`) |
| `rotary-volume.ko` | Энкодер регулятора громкости | GPIO interrupts |
| `gpio_keypad.ko` | Аппаратные кнопки управления | Polled GPIO |
| `zigbee-control.ko` | Управление сбросом и bootloader модуля Zigbee EFR32 | GPIO |
| `ltr308als01.ko` | Датчик внешней освещённости | I2C / IIO |
| `leds-is31fl32xx.ko` | LED-контроллер IS31FL3236A | I2C |
| `snd-soc-sy6045s.ko` | Усилитель НЧ/ВЧ с DSP biquad-фильтрами | I2C / I2S |
| `snd-soc-es8156.ko` | Аудио ЦАП линейного выхода / наушников | I2C / I2S |
| `snd-soc-es7210.ko` | 4-канальный АЦП микрофонного массива | I2C / TDM |
| `snd-soc-tas5825m.ko` | Усилитель TAS5825M | I2C / I2S |

---

## Референсная конфигурация: `station-max` (Amlogic S905X3 / SM1)

Для платы `station-max` (`board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts`):
* **Фреймбуферы:** `/dev/fb0` (HDMI `mesondrmfb`), `/dev/fb1` (`/dev/fb_screen`, матрица 25×16), `/dev/fb2` (`/dev/fb_ring`, 24 RGB-светодиода).
* **Беспроводные интерфейсы:** Realtek RTL8822CS (Wi-Fi SDIO `rtw88_8822cs`, Bluetooth 5.0 Serdev `/dev/ttyAML1`), Zigbee Silicon Labs EFR32 (`/dev/ttyAML2`).
* **Аудио (`AML-AUGESOUND`):** НЧ/ВЧ усилители SY6045S (`2-002b`, `2-002a`) с DSP-калибровкой, ЦАП ES8156 (`2-0008`), АЦП ES7210 (`2-0040`) и 8-канальный PDM-массив микрофонов.

---

## Единая утилита `atrios`

Все операции управления системой, периферией, обновлениями и сбросом собраны в `/usr/bin/atrios`:

```bash
# Общий статус и система
atrios status                           # Обзор Wi-Fi, Bluetooth и Zigbee
atrios info                             # Системная сводка (fastfetch)
atrios check                            # Аудит аудиотракта и шин

# Экран, LED-кольцо и датчики
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
atrios ota status                                   # Версия, статус SWUpdate и бэкапы .bak
atrios ota install <pkg.tar.gz|.swu|URL> [--reboot] # Проверка SHA-256 и установка OTA
atrios ota rollback [--reboot]                      # Откат ядра и DTB из .bak
atrios reset                                        # Полный сброс устройства (двойное подтверждение)
```

---

## OTA-обновления и Factory Reset

### OTA / SWUpdate (`atrios ota`)
* Поддерживает пакеты `.tar.gz` с `manifest.json` (проверка `device_id` из `device-detect` и контрольных сумм SHA-256 до начала записи) и образы `.swu` (через `swupdate`, если бинарник установлен), включая загрузку с сервера по URL (`http(s)://...`).
* Обновляет ядро `Image` и DTB устройства одновременно в `/boot` и на загрузочном разделе, сохраняя резервные копии `.bak` для отката (`atrios ota rollback`), а также применяет rootfs-архивы и `.deb`-пакеты.

### Factory Reset (`atrios reset`)
* Требует двойного интерактивного подтверждения (`YES` → `RESET`; для скриптов поддерживается `--yes`).
* Сравнивает установленные `dpkg`-пакеты с эталоном `/etc/atrios-base-packages.list` и удаляет все доустановленные пользователем пакеты (`apt-get purge` + `autoremove`), `pip`-пакеты, аккаунты с `UID >= 1000` и `/home/*`, учётные данные Wi-Fi, сопряжения Bluetooth, SSH/Dropbear-ключи, истории и состояние `/var/lib/atrios/*`.

---

## Сборка, верификация и прошивка

```bash
# Проверка компиляции DTS, драйверов и userspace-скриптов
./scripts/verify_all.sh

# Сборка ядра и образа
./scripts/build_kernel_and_image.sh

# Сборка Mainline U-Boot
./scripts/build_mainline_uboot.sh
```

### Прошивка (Amlogic USB Burning Tool / Fastboot)
1. **USB Burning Tool:** откройте собранный `.img` (`File -> Import image`), нажмите **Start**, переведите устройство в BootROM DFU и подключите по USB.
2. **UART + Fastboot:** прервите загрузку в U-Boot (`fastboot 0`), затем прошейте разделы:
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
