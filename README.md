# MainlineAtriOS (AtriOS-aml)

**MainlineAtriOS** — порт операционной системы AtriOS на **Mainline Linux Kernel (v7.0+)** и современный **Mainline U-Boot** для Яндекс Станции Макс (Yandex Station Max / Quasar 2, SoC Amlogic S905X3 / SM1) от **leftymods**.

Проект полностью избавляется от устаревшего ядра Amlogic 4.9.113 и переносит аппаратную платформу на современное апстримное ядро Linux с открытыми драйверами, обновленным стеком Buildroot и поддержкой прошивки по USB (AML Burn Mode).

---

## Архитектура и компоненты

### 1. Mainline U-Boot Bootloader
* **Конфигурация:** `board/yandex/station2/mainline/uboot_station_max_defconfig`
* **Device Tree:** `board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max-u-boot.dtsi`
* **Скрипт сборки и FIP-подписи:** `scripts/build_mainline_uboot.sh`
* **Режим прошивки:** Включен режим USB OTG (`dr_mode = "otg"`), `fastboot` и автозапуск `aml_burn` при подключении кабеля USB к ПК.

### 2. Mainline Linux 7.0 Kernel (`AtriOS-aml`)
* **Имя ядра:** `AtriOS-aml` (`CONFIG_LOCALVERSION="-AtriOS-aml"`)
* **Производитель / модель:** `leftymods` (`model = "AtriOS-aml Station Max (leftymods)"`, `compatible = "leftymods,station-max", "amlogic,sm1"`)
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

## Сборка

### 1. Сборка корневой системы (Buildroot 2026.08.1):
```bash
cd buildroot
make defconfig BR2_DEFCONFIG=../board/yandex/station2/mainline/buildroot_station_max_defconfig
make -j$(nproc)
```

### 2. Сборка ядра Linux 7.0 (`AtriOS-aml`):
```bash
cd /path/to/linux-leftymods
cp /path/to/MainlineAtriOS/board/yandex/station2/mainline/linux_7.0_station_max_defconfig .config
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules
```

### 3. Сборка драйверов:
```bash
cd drivers/atrios
make -C /path/to/linux-leftymods M=$(pwd) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
```

### 4. Компиляция Device Tree:
```bash
cpp -nostdinc -I /path/to/linux/include -I /path/to/linux/arch/arm64/boot/dts/amlogic -I /path/to/linux/arch/arm64/boot/dts \
    -undef -x assembler-with-cpp board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts | \
    dtc -I dts -O dtb -o meson-sm1-yandex-station-max.dtb
```

### 5. Сборка Mainline U-Boot:
```bash
./scripts/build_mainline_uboot.sh
```

---

## Авторство и лицензия
* **Разработчик:** leftymods <ggalab33@gmail.com>
* **Лицензия:** GPL-2.0-or-later / MIT
