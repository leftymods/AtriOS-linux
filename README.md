# AtriOS Linux — Yandex Station Max (S905X3 Mod)

<p align="center">
  <strong>Встроенный дистрибутив Linux для Яндекс Станции Макс с заменённым SoC на Amlogic S905X3</strong>
  <br />
  <em>Чистый Linux без Android, облачной привязки и сервисов AOSP для умной колонки Яндекс Станция Макс.</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Target-Yandex%20Station%20Max-0284c7?style=flat-square" alt="Target" />
  <img src="https://img.shields.io/badge/Hardware%20Mod-S905X3%20(SM1)%20Swap-f97316?style=flat-square" alt="SoC Mod" />
  <img src="https://img.shields.io/badge/Architecture-ARM64%20(aarch64)-3b82f6?style=flat-square" alt="Architecture" />
  <img src="https://img.shields.io/badge/Buildroot-2024.02%20LTS-16a34a?style=flat-square" alt="Buildroot" />
  <img src="https://img.shields.io/badge/Kernel-4.9.113%20(SMP%20PREEMPT)-7c3aed?style=flat-square" alt="Kernel" />
  <img src="https://img.shields.io/badge/Audio-ALSA%20%7C%20PipeWire%20%7C%20SY6045S-8b5cf6?style=flat-square" alt="Audio" />
  <img src="https://img.shields.io/badge/CLI-Unified%20atrios%20tool-059669?style=flat-square" alt="CLI" />
  <img src="https://img.shields.io/badge/License-GPL--2.0-64748b?style=flat-square" alt="License" />
</p>

> [!NOTE]
> **Проверено на реальном устройстве**: прошивка стабильно загружается на Яндекс Станции Макс с аппаратной заменой процессора на S905X3.

---

## О проекте

**AtriOS Linux** — независимый дистрибутив встроенного Linux от **leftymods** для умной колонки **Яндекс Станция Макс** с аппаратной заменой SoC на **Amlogic S905X3**.

Проект полностью заменяет проприетарную систему Android 9 на минималистичное и быстрое окружение на базе **Buildroot** с собственной сборкой драйверов, сервисов и управляющей утилиты.

### Что даёт проект

- **Мгновенный старт**: загрузка напрямую через `/sbin/init` (Busybox), без Android/AOSP, Zygote и фоновых сервисов.
- **Прямой контроль периферии**: управление экраном 25×16 Gowin FPGA, RGB-кольцом, датчиком света, кнопками и энкодером.
- **Готовый сетевой стек**: поддержка Wi‑Fi 2,4/5 ГГц (RTL8822CS) и Bluetooth 5.0 (в отладке).
- **Единый инструмент управления**: встроенная утилита `atrios` для всех функций колонки.
- **Самодостаточный репозиторий**: включает инструменты сборки, прошивок и патчи для платформы.

---

## Быстрый старт

### Требования

- Яндекс Станция Макс с аппаратной заменой SoC на S905X3
- Linux / WSL2 для сборки
- USB-UART переходник
- Amlogic USB Burning Tool
- устройство в режиме Recovery

### Проверка после загрузки

Подключитесь по UART к консоли (`ttyS0`, 115200 8N1):

```bash
# 1. Проверить загруженные модули ядра
lsmod

# 2. Проверить экран
atrios screen ticker "ATRIOS"

# 3. Проверить RGB-кольцо
atrios ring rainbow

# 4. Проверить датчик освещённости
atrios sensor

# 5. Проверить кнопки и энкодер
atrios input

# 6. Проверить сетевые интерфейсы
atrios status
```

---

## Статус аппаратных компонентов

| Компонент | Железо / Чип | Шина / Интерфейс | Драйвер / Модуль | Статус |
|:---|:---|:---|:---|:---|
| **Загрузчик (U-Boot)** | Native S905X3 Bootloader | eMMC / USB Burn | U-Boot 2015.01 | Работает |
| **UART-консоль** | Amlogic UART0 (115200 8N1) | `/dev/ttyS0` | Интерактивный root shell | Работает |
| **LED-экран (25×16)** | Gowin GW1N FPGA | SPI1 + CS (`GPIOH_7`) | `gowin_led_device` (`/dev/fb4`) | Работает |
| **RGB-кольцо** | 2× IS31FL3236A (24 RGB / 72 LED) | I2C0 (`0x3c`, `0x3f`) | `quasar_led_ring.ko` (`/dev/fb5`) | Работает |
| **Датчик света** | Lite-On LTR-308ALS | I2C0 (`0x53`) | `ltr308als01` (`iio:device0`) | Работает |
| **Крутилка громкости** | Двухфазный квадратурный энкодер | `GPIOH_6`, `GPIOAO_10` | `rotary-volume.ko` | Работает |
| **Верхние кнопки** | Кнопка «Алиса» и Mute | `GPIOA_15`, `GPIOA_14` | `gpio_keypad` (DTS poll 20ms) | Работает |
| **Wi‑Fi** | Realtek RTL8822CS (802.11ac 2×2) | SDIO (`sd_emmc_a`) | `88x2es.ko` + `/dev/wifi_power` | Работает |
| **Bluetooth** | Realtek RTL8822CS Bluetooth 5.0 | UART_A (`/dev/ttyAML1`) | `rtk_bt` (`hci0`) | В отладке |
| **HDMI-видеовыход** | MPI7002 / Generic HDMI TX | HDMI TX (`osd1` / `fb0`) | `/dev/fb0` (1024×600 @ 60Hz) | Работает |
| **Zigbee** | Silicon Labs EFR32 | UART_AO_B (`/dev/ttyAML4`) | `atrios zigbee` / reset GPIO | В отладке |
| **Микрофонный массив** | Everest ES7210 4-ch ADC | TDM-C, I2C1 | `snd-soc-es7210` (8 каналов) | Не проверено |
| **Динамики** | Silergy SY6045S (20V PVDD) | TDM-B, I2C2 (`0x2a`, `0x2b`) | `snd-soc-sy6045s` | Не работает |
| **Аудиовыход 3.5 мм** | Everest ES8156 DAC | TDM-B, I2C2 (`0x10`) | `snd-soc-es8156` | Не работает |

> [!WARNING]
> **Звуковая подсистема находится в доработке**: драйверы ядра регистрируются корректно, но физическое и программное аудио-пути ещё требуют финальной стабилизации.

---

## Консольная утилита `atrios`

Все управляющие функции объединены в один бинарный диспетчер: `/usr/bin/atrios`.

```bash
atrios [команда] [параметры]
```

### Доступные команды

| Команда | Описание | Пример |
|:---|:---|:---|
| `status` | Состояние сетевых интерфейсов | `atrios status` |
| `screen` | Управление матричным экраном 25×16 | `atrios screen kisser [1\|2\|animate]` |
| `ring` | Управление RGB-кольцом | `atrios ring rainbow` |
| `sensor` | Считывание уровня освещённости | `atrios sensor` |
| `input` | Мониторинг кнопок и энкодера | `atrios input` |
| `wifi` | Сканирование, подключение, питание Wi‑Fi | `atrios wifi scan` |
| `bt` | Инициализация и сканирование Bluetooth | `atrios bt init` |
| `zigbee` | Сброс и диагностика Zigbee-модуля | `atrios zigbee reset` |
| `volume` | Установка уровня громкости | `atrios volume 80` |
| `mute` | Отключение / включение микрофонов | `atrios mute on` |
| `amps` | Проверка статуса усилителей | `atrios amps` |
| `test` | Комплексный тест аудио и микрофонов | `atrios test` |

---

## Системные службы

Фоновые демоны запускаются при старте системы через `/etc/init.d/S60atrios`:

- **Часы на экране**: `/usr/lib/atrios/screend`
- **Обработка энкодера**: `/usr/lib/atrios/rotaryd`
- **Индикация**: отключение микрофона физической кнопкой не блокирует красное свечение кольца

Интерактивная командная оболочка:

- `sh` используется как стабильный системный login-shell
- `~/.profile` безопасно запускает `fish`, если поддержка терминала доступна

---

## Структура проекта

```text
AtriOS-Android/
├── board/yandex/station2/
│   ├── rootfs_overlay/           # Файлы корневой файловой системы
│   │   ├── etc/                  # Конфигурации ALSA, init-скрипты, Wi-Fi
│   │   ├── lib/firmware/         # Бинарные прошивки (FPGA, SY6045S, Realtek)
│   │   ├── lib/modules/4.9.113/  # Модули ядра (экран, кольцо, звук, сенсор)
│   │   └── usr/
│   │       ├── bin/atrios        # Единая утилита управления
│   │       └── lib/atrios/       # Внутренние модули и демоны
│   └── station2_defconfig        # Конфигурация Buildroot для Станции Макс
├── common/                       # Исходный код ядра Linux 4.9.113 и DTS
├── tools/
│   ├── bin/                      # Утилиты сборщика (ampack, make_ext4fs, dtbTool)
│   └── fw_builder/               # Скрипты компоновки прошивки (fw_builder.py)
│       └── overlay/dts/          # Финальное дерево устройств (DTS) под S905X3
├── build_atrios.sh               # Главный скрипт сборки прошивки
└── README.md                     # Документация проекта
```

---

## Сборка прошивки

Сборка выполняется в окружении Linux / WSL2:

```bash
cd /home/lmods/AtriOS-Android

# Запуск сборки с указанием номера версии (например, v112)
./build_atrios.sh 112
```

Результат сборки — готовый образ для прошивки:

```text
tools/fw_builder/yandex_s905x3_custom_v112.img
```

Размер образа примерно: `~1.46 ГБ`.

### Прошивка устройства

Прошивка выполняется через стандартный **Amlogic USB Burning Tool** в режиме восстановления (Recovery) либо через альтернативный загрузочный инструмент, если он используется в вашей сборке.

---

## Примечания

- Проект является экспериментальным и рассчитан на пользователей, готовых работать с нестандартным железом и Linux-сборкой.
- Не все подсистемы полностью стабилизированы: звук, Bluetooth и Zigbee требуют дополнительной доводки.
- Для диагностики и отладки рекомендуется использовать UART-консоль.

---

## Контакты и ссылки

- Репозиторий: `leftymods/AtriOS-linux`
- Автор: `leftymods`
- Тема проекта: Linux для Яндекс Станции Макс с модом S905X3
