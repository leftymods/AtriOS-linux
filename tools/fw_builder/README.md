# 🚀 AtriStation OS — AtriStation Max (Amlogic S905X3 / SM1)

<div align="center">

![Platform](https://img.shields.io/badge/Platform-Amlogic%20S905X3%20%28SM1%20Rev%20B%29-blue.svg?style=for-the-badge)
![Android](https://img.shields.io/badge/Android-9.0%20Pie%20%28API%2028%29-green.svg?style=for-the-badge)
![Kernel](https://img.shields.io/badge/Kernel-Linux%204.9.113%20%28SMP%20PREEMPT%29-orange.svg?style=for-the-badge)
![Security](https://img.shields.io/badge/AVB%202.0-RSA%204096--bit-red.svg?style=for-the-badge)
![Manufacturer](https://img.shields.io/badge/Manufacturer-leftymods-purple.svg?style=for-the-badge)
![Device](https://img.shields.io/badge/Device-AtriStation%20Max-cyan.svg?style=for-the-badge)

**Official custom firmware distribution for AtriStation Max hardware platform by leftymods.**

[English Documentation](#-english-documentation) • [Русская документация](#-русская-документация)

</div>

---

<a name="-english-documentation"></a>
## 📖 English Documentation

### 1. Overview & Architecture

**AtriStation OS** is a high-performance, modular operating system and firmware distribution developed by **leftymods** for the **AtriStation Max** smart speaker hardware platform, powered by the **Amlogic S905X3 (SM1 Rev B)** quad-core ARM Cortex-A55 processor.

#### 🌟 Architectural Pillars
* **Strict Stock Partition Preservation**: Android system, vendor, odm, and product partitions maintain complete compatibility with official cloud and voice assistant stacks.
* **Low-Level Hardware Adaptation Layer**: Hardware adaptations are isolated exclusively to the custom Device Tree (`sm1_s905x3_yandex.dts`), Linux kernel 4.9.113 drivers, audio routing, and display handover.
* **Full AVB 2.0 Secure Boot Cryptographic Signing**: All 8 critical partitions (`boot`, `recovery`, `dtbo`, `system`, `vendor`, `odm`, `product`, `vbmeta`) are cryptographically sealed with custom RSA-4096 keys (`keys/atristation_priv.pem`) using Google's `avbtool`.
* **Automated Firmware Engine (`fw_builder.py`)**: 100% native execution in WSL2 Linux with zero bridging overhead, automated unpacking, patch injection, ext4 regeneration, and Amlogic v2 burning image creation.

---

### 2. Hardware Subsystems Status Matrix

| Subsystem | Hardware Component / Chip | Bus / Pin Routing | Status | Implementation Details |
|---|---|---|:---:|---|
| **HDMI Display** | MPI7002 (1024x600 @ 60Hz HDMI) | HDMI TX (`osd1` / `fb0`) | ✅ **ACTIVE** | Custombuilt modeline locked in driver. Patched `osd_hw.c` (bypasses `systemcontrol` screen blanking) and `hdmi_tx_hw.c` (forces `CLR_AVMUTE`), ensuring uninterrupted video from U-Boot logo to UI. |
| **Sound & Amps** | Everest ES8156 DAC + Silergy SY6045S 20V Amps | TDM-B, I2C2 (`0x08`, `0x2a`, `0x2b`), Pin 75 (`GPIOX_10`) | ✅ **ACTIVE** | DAPM output route wired to `"Headphones\0Headphone LOUT\0Headphones\0Headphone ROUT"`. `fixedregulator-20v-amp` active-high on Pin 75 (`GPIOX_10`) with `3V3_D` and `1V8_D` rails for clean anti-pop 20V rail power. |
| **Mini LED Screen** | Gowin GW1N-4B FPGA SPI (`gowin_led_device`) | SPI1 @ 4MHz, Pin 74 (`GPIOX_8`), CS: `GPIOH_7` | ✅ **ACTIVE** | Freed Pin 74 (`GPIOX_8`) by disabling `tdm@0` collision. Renders clock, facial expressions, and weather matrix cleanly on `fb4`. |
| **Top LED Ring** | 2x Lumissil IS31FL3236A (`quasar_led_ring`) | I2C0 (`0x3c`, `0x3f`), `fb5` (`led_ring_fb`) | ✅ **ACTIVE** | Restored `CONFIG_FB_BACKLIGHT=y` ABI layout in kernel. Native patterns and dynamic color animations rendered on top ring. |
| **Zigbee Controller**| Silicon Labs EFR32 (`zb_control`) | UART_AO_B, Pin 82 (`GPIOX_16`), RST: `GPIOX_17` | ✅ **ACTIVE** | Freed Pin 82 from `wifi` PWM collision in DTS. Operational on `/dev/ttyAML4`. |
| **Wi-Fi & Bluetooth**| Realtek RTL8822CS / Broadcom Wireless SDIO | `sd_emmc_a`, `uart_A`, WL_REG_ON: `GPIOX_7`, BT_EN: `GPIOX_18` | ✅ **ACTIVE** | SDIO & UART wireless drivers active. Wi-Fi uses direct PWM 32.768kHz clock (`pwm_e_clk`); Bluetooth online via `hci0`. |
| **Rotary Knob** | Dual-Phase Rotary Encoder (AB Phase) | `GPIOAO_10` (Phase B), `GPIOH_6` (Phase A) | ✅ **ACTIVE** | Direct hardware interrupt handling mapped to Android audio system volume steps. |
| **Light Sensor** | Lite-On LTR-308ALS-01 | I2C0 (`0x53`) | ✅ **ACTIVE** | Dynamic ambient illuminance feedback automatically adjusts front screen backlight level. |
| **AVB 2.0 Security** | Custom RSA 4096-bit Key Pair | All 8 partitions & `vbmeta.PARTITION` | ✅ **ACTIVE** | Custom cryptographic root-of-trust with `SHA256_RSA4096` hash and hashtree verification. |
| **Root Serial Shell**| Android Root Shell (`/system/bin/sh`) | UART0 (`ttyS0`, 115200 8N1) | ✅ **ACTIVE** | Configured with `androidboot.console=ttyS0 console=ttyS0,115200` and `ro.debuggable=1`. |

---

### 3. Firmware Builder Suite (`fw_builder.py`)

The automated assembly suite provides unified CLI operations for firmware compilation, live inspection, and automated daemon watching.

```
tools/fw_builder/
├── builder/
│   ├── avb.py           # AVB 2.0 RSA-4096 signing engine
│   ├── cli.py           # Unified CLI interface & commands
│   ├── dts_boot.py      # Device tree & boot.PARTITION packager
│   ├── mount.py         # Ext4 loop mount manager
│   ├── nobase.py        # Staging synthesis from in-repo assets
│   ├── repack.py        # Amlogic v2 image packer & patcher
│   └── watcher.py       # Live directory watcher daemon
└── fw_builder.py        # Main executable entry point
```

#### CLI Command Reference

```bash
# 1. Clean build from scratch (repository assets only):
python3 fw_builder.py build --from-scratch

# 2. Incremental build (repack changed components only):
python3 fw_builder.py build

# 3. Mount ext4 partition for direct inspection & modification:
python3 fw_builder.py mount vendor
python3 fw_builder.py mount system --read-only

# 4. Unmount active partitions cleanly:
python3 fw_builder.py unmount vendor
python3 fw_builder.py unmount --all

# 5. Check environment and cache status:
python3 fw_builder.py status

# 6. Purge temporary files and cache:
python3 fw_builder.py clean
```

---

<a name="-русская-документация"></a>
## 🇷🇺 Русская документация

### 1. Архитектура и описание платформы

**AtriStation OS** — специализированный дистрибутив операционной системы от **leftymods** для умной колонки **AtriStation Max** на базе процессора **Amlogic S905X3 (SM1 Rev B)**.

#### 🛡 Ключевые принципы
1. **Сохранение стоковых разделов**: Разделы `system`, `vendor`, `odm`, `product` сохраняют полную функциональность и совместимость со всеми сервисами.
2. **Изолированный уровень адаптации**: Все аппаратные правки локализованы в Device Tree (`sm1_s905x3_yandex.dts`), кастомном ядре Linux 4.9.113 и правилах инициализации.
3. **Полная криптографическая подпись AVB 2.0**: Все 8 разделов подписываются приватным RSA-4096 ключом (`keys/atristation_priv.pem`) через `avbtool`.
4. **Автоматизированный пайплайн сборки**: Инструмент `fw_builder.py` собирает готовый `.img` одной командой из ресурсов репозитория (загрузчик, U-Boot, логотип, DTS, ядро, вендорные модули) без использования стоковой прошивки.

---

### 2. Сводная таблица поддержки оборудования

| Подсистема | Компонент / Чип | Шина / Пины | Статус | Детали реализации |
|---|---|---|:---:|---|
| **HDMI Экран** | MPI7002 (1024x600 @ 60Hz HDMI) | HDMI TX (`osd1` / `fb0`) | ✅ **РАБОТАЕТ** | Modeline заблокирован в драйвере. Пропатчены `osd_hw.c` и `hdmi_tx_hw.c` (`CLR_AVMUTE`), исключая гашение экрана. |
| **Звук и Усилители**| ES8156 ЦАП + SY6045S 20V Усилители | TDM-B, I2C2 (`0x08`, `0x2a`, `0x2b`), Pin 75 | ✅ **РАБОТАЕТ** | Маршрут DAPM подключен к Headphones. Питание 20V (`20V_AMPL`) на Pin 75 с плавным включением без щелчков. |
| **LED Матрица** | Gowin GW1N-4B FPGA SPI (`gowin_led_device`)| SPI1 @ 4MHz, Pin 74 (`GPIOX_8`), CS: `GPIOH_7` | ✅ **РАБОТАЕТ** | Устранен конфликт Pin 74 с TDMA. Отображает часы, анимацию Алисы и статус на `fb4`. |
| **Верхнее кольцо**| 2x Lumissil IS31FL3236A (`quasar_led_ring`) | I2C0 (`0x3c`, `0x3f`), `fb5` | ✅ **РАБОТАЕТ** | Восстановлен `CONFIG_FB_BACKLIGHT=y` ABI layout. Динамическая подсветка и эффекты активны. |
| **Zigbee Модуль** | Silicon Labs EFR32 (`zb_control`) | UART_AO_B, Pin 82 (`GPIOX_16`), RST: `GPIOX_17` | ✅ **РАБОТАЕТ** | Освобожден Pin 82 от PWM Wi-Fi. Порт `/dev/ttyAML4` активен. |
| **Wi-Fi и Bluetooth**| Realtek RTL8822CS / Broadcom SDIO | `sd_emmc_a`, `uart_A`, WL_REG_ON: `GPIOX_7` | ✅ **РАБОТАЕТ** | Драйверы SDIO и UART активны. Тактирование 32.768 кГц через `pwm_e_clk`. |
| **Энкодер громкости**| Двухфазный энкодер (AB Phase) | `GPIOAO_10` (Phase B), `GPIOH_6` (Phase A) | ✅ **РАБОТАЕТ** | Аппаратные прерывания транслируются напрямую в системную громкость Android. |
| **Датчик света** | Lite-On LTR-308ALS-01 | I2C0 (`0x53`) | ✅ **РАБОТАЕТ** | Автоматическая регулировка яркости фронтального экрана по уровню освещенности. |
| **Безопасность AVB**| RSA 4096-bit приватный ключ | Все 8 разделов + `vbmeta.PARTITION` | ✅ **РАБОТАЕТ** | Хэширование и хэш-деревья `SHA256_RSA4096` через официальный `avbtool`. |
| **Консоль UART** | Root Shell (`/system/bin/sh`) | UART0 (`ttyS0`, 115200 8N1) | ✅ **РАБОТАЕТ** | Интерактивный терминал активен (`androidboot.console=ttyS0 console=ttyS0,115200`). |

---

### 3. Быстрый старт сборки прошивки

```bash
# Клонирование и переход в репозиторий:
cd AtriOS-Android

# Полная сборка с нуля (только ресурсы репозитория):
python3 tools/fw_builder/fw_builder.py build --from-scratch

# Проверка статуса сборщика и кэша:
python3 tools/fw_builder/fw_builder.py status
```

---

<div align="center">

**Maintainer**: leftymods • **Target**: AtriStation Max (Amlogic S905X3)

</div>
