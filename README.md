# MainlineAtriOS

**MainlineAtriOS** — порт операционной системы AtriOS на **Mainline Linux Kernel (v7.0+)** и современный **Mainline U-Boot** для Яндекс Станции Макс (Yandex Station Max / Quasar 2, SoC Amlogic S905X3 / SM1).

Проект полностью избавляется от закрытого устаревшего ядра Amlogic 4.9.113 и переносит аппаратную платформу на современное апстримное ядро Linux с открытыми драйверами.

---

## Архитектура и компоненты

### 1. Mainline U-Boot Bootloader
* **Конфигурация:** oard/yandex/station2/mainline/uboot_station_max_defconfig
* **Device Tree:** oard/yandex/station2/mainline/dts/meson-sm1-yandex-station-max-u-boot.dtsi
* **Скрипт сборки и FIP-подписи:** scripts/build_mainline_uboot.sh

### 2. Mainline Linux 7.0 Kernel & Device Tree
* **Device Tree:** oard/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts
* **Defconfig:** oard/yandex/station2/mainline/linux_7.0_station_max_defconfig

### 3. Восстановленные драйверы (drivers/yandex/)
Все 13 проприетарных драйверов переписаны из бинарных модулей и портированы под Kernel API 7.0+.

---

## Сборка

### Сборка Mainline U-Boot:
`ash
./scripts/build_mainline_uboot.sh
`

### Сборка драйверов:
`ash
cd drivers/yandex
make KDIR=/path/to/linux-7.0 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
`
