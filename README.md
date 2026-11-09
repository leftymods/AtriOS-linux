# AtriOS Nebula

**AtriOS Nebula** — дистрибутив операционной системы на базе **Mainline Linux Kernel (v7.0+, ядро `AtriOS-aml`)** и современного **Mainline U-Boot** для Яндекс Станции Макс (Yandex Station Max / Quasar 2, SoC Amlogic S905X3 / SM1) от **leftymods**.

Проект полностью избавляется от устаревшего закрытого ядра Amlogic 4.9.113 и переносит аппаратную платформу на современное апстримное ядро Linux с открытыми драйверами, обновленным стеком Buildroot 2026.08.1 LTS и поддержкой прошивки по USB (AML Burn Mode).

---

## Именование компонентов
* **Операционная система (дистрибутив):** `AtriOS Nebula` (хостнейм `atrios-nebula`)
* **Ядро Linux:** `AtriOS-aml` (`CONFIG_LOCALVERSION="-AtriOS-aml"`)
* **Производитель / вендор:** `leftymods` (`compatible = "leftymods,station-max", "amlogic,sm1"`)

---

## Архитектура и компоненты

### 1. Mainline U-Boot Bootloader
* **Конфигурация:** `board/yandex/station2/mainline/uboot_station_max_defconfig`
* **Device Tree:** `board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max-u-boot.dtsi`
* **Скрипт сборки и FIP-подписи:** `scripts/build_mainline_uboot.sh`
* **Режим прошивки:** Включен режим USB OTG (`dr_mode = "otg"`), `fastboot` и автозапуск `aml_burn` при подключении кабеля USB к ПК.

### 2. Mainline Linux 7.0 Kernel (`AtriOS-aml`)
* **Имя ядра:** `AtriOS-aml`
* **Device Tree:** `board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts`
* **Defconfig ядра:** `board/yandex/station2/mainline/linux_7.0_station_max_defconfig`

### 3. Userspace & Toolchain (Buildroot 2026.08.1 LTS)
* **Каталог сборки:** `buildroot/` (Buildroot 2026.08.1)
* **Конфигурация:** `board/yandex/station2/mainline/buildroot_station_max_defconfig`
* **Стек:** PipeWire + WirePlumber, BlueZ 5, wpa_supplicant, ALSA, Python 3.

### 4. Адаптированные драйверы (`drivers/atrios/`)
Все 13 проприетарных драйверов переписаны из бинарных модулей и адаптированы под Kernel API 6.18+ / 7.0+:

| Модуль | Назначение | Интерфейс |
|---|---|---|
| `gowin_led_device.ko` | Контроллер экрана FPGA Gowin | SPI + GPIO / UART |
| `quasar_led_screen.ko` | Протокол отображения LED матрицы | SPI bus |
| `quasar_led_ring.ko` | Верхнее кольцо адресных светодиодов | RGB LED / PWM |
| `quasar_pcba_ids.ko` | Детекция ревизии платы через АЦП | IIO ADC |
| `rotary-volume.ko` | Энкодер регулятора громкости | GPIO interrupts |
| `gpio_keypad.ko` | Аппаратные кнопки управления | Polled GPIO |
| `zigbee-control.ko` | Сброс и bootloader модуля Zigbee EFR32 | GPIO |
| `ltr308als01.ko` | Датчик внешней освещенности | I2C / IIO |
| `leds-is31fl32xx.ko` | Драйвер светодиодной подсветки IS31FL | I2C |
| `snd-soc-sy6045s.ko` | Усилитель НЧ/ВЧ с DSP biquad фильтрами | I2C / I2S |
| `snd-soc-es8156.ko` | Аудио ЦАП линейного выхода / наушников | I2C / I2S |
| `snd-soc-es7210.ko` | 4-канальный АЦП микрофонного массива | I2C / TDM |
| `snd-soc-tas5825m.ko` | Дополнительный стерео УМЗЧ TAS5825M | I2C / I2S |

---

## Аппаратные интерфейсы Станции Макс

* **Фреймбуферы:**
  * `/dev/fb0` — Фронтальный монохромный LED экран Gowin 25x16 (RGB888 1200 байт). Демон `screend` выводит крупные цифровые часы и анимации.
  * `/dev/fb1` — Верхнее кольцо из 24 RGB-светодиодов на сдвоенном драйвере IS31FL3236A (RGB888 576 байт). Демон `rotaryd` плавно отображает уровень громкости.
* **Беспроводные модули:**
  * **Wi-Fi (Realtek RTL8822CS):** шина SDIO (`sd_emmc_a`), двухдиапазонный 2.4/5 ГГц, WPA2/WPA3.
  * **Bluetooth 5.0 (Realtek RTL8822CS):** скоростной UART Serdev (`uart_A`, `/dev/ttyAML1`, 1.5 Мбод). Поддерживает A2DP Sink (режим Bluetooth-колонки) и BLE.
  * **Zigbee (Silicon Labs EFR32 MG21):** UART (`uart_AO_B`, `/dev/ttyAML2`), протокол EZSP NCP, управление сбросом через `zigbee-control`.
* **Аудиосистема (ASoC AML-AUGESOUND):**
  * НЧ-усилитель (SY6045S, I2C `2-002b`) и ВЧ-усилитель (SY6045S, I2C `2-002a`) с загрузкой калибровочных DSP biquad/DRC фильтров.
  * 8-канальный цифровой микрофонный массив (PDM контроллер, 5 линий GPIOZ, кодек `dmics`).
  * ЦАП наушников/AUX ES8156 (`2-0008`) и 4-канальный АЦП ES7210 (`2-0040`).

---

## Тестирование и верификация (CI Suite)

В репозиторий включен автоматизированный 5-уровневый скрипт проверки целостности всех компонентов:
```bash
./scripts/verify_all.sh
```
Скрипт проверяет:
1. Кросс-компилятор `aarch64-linux-gnu-gcc`, `dtc` и Python окружение.
2. Препроцессинг и компиляцию Device Tree (`meson-sm1-yandex-station-max.dtb`), включая Serdev BT, PDM, аудиокарту.
3. Чистую компиляцию всех 13 ядерных драйверов AtriOS под Linux 7.x.
4. Целостность DSP прошивок SY6045S и микрокода Bluetooth Realtek.
5. Синтаксис и запуск юзерспейс демонов (`screend`, `rotaryd`, `bt-agent`, `wifi_scan.py`, init-скриптов и CLI `/usr/bin/atrios`).

---

## Управление через CLI (`atrios`)

На работающей станции доступна единая утилита управления:
```bash
atrios status                      # Статус Wi-Fi, Bluetooth, Zigbee
atrios wifi scan                   # Сканирование 2.4/5 ГГц сетей
atrios wifi connect <SSID> <PASS>  # Подключение к Wi-Fi
atrios bt speaker on|off           # Режим Bluetooth-колонки
atrios screen clock|kisser|off     # Часы или анимации на экране
atrios ring rainbow|volume N|off   # Управление LED-кольцом
atrios zigbee ping|reset           # Диагностика EFR32 Zigbee
```

---

## Сборка образа AtriOS Nebula

### Полная сборка прошивки в один шаг:
```bash
./scripts/build_nebula_image.sh
```

---

## Как прошить Яндекс Станцию Макс

### Способ 1: Через Amlogic USB Burning Tool (Рекомендуемый)
1. Установите **Amlogic USB Burning Tool** (версии 2.2.0 или новее) на ПК с Windows.
2. В программе выберите: `File -> Import image` и укажите собранный `AtriOS-Nebula-station-max-burn.img`.
3. Нажмите кнопку **Start**.
4. Подключите колонку кабелем USB (Type-A на Type-A или Type-C в сервисный порт) к ПК:
   - Зажмите кнопку **Mute** (микрофон выключен) на верхней панели и подключите штатный блок питания 12V.
   - Колонка перейдет в режим BootROM DFU (в диспетчере устройств Windows появится `WorldCup Device`).
5. Процесс прошивки начнется автоматически. По завершении (100% зеленый статус) нажмите **Stop**, отключите USB и перезагрузите Станцию.

### Способ 2: Через UART консоль и Fastboot
1. Подключите USB-UART TTL адаптер (3.3V) к контактам TX, RX, GND (скорость 115200 8N1).
2. При подаче питания нажмите любую клавишу для прерывания в U-Boot.
3. Введите команду:
   ```bash
   fastboot 0
   ```
4. На ПК через утилиту fastboot:
   ```bash
   fastboot flash bootloader u-boot.bin
   fastboot flash boot boot.img
   fastboot flash rootfs rootfs.ext4
   fastboot reboot
   ```

---

## Авторство и лицензия
* **Разработчик:** leftymods <ggalab33@gmail.com>
* **Лицензия:** GPL-2.0-or-later / MIT
