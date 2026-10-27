# AtriOS Linux — Yandex Station Max (S905X3 Mod)

<p align="center">
  <strong>Встроенный дистрибутив Linux для Яндекс Станции Макс с заменённым SoC на Amlogic S905X3</strong>
  <br />
  <em>Чистый Linux без сервисов Android и облачной привязки для умной колонки Яндекс Станция Макс (аппаратный мод S905X3)</em>
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
> **Проверено на реальном устройстве**:
> Прошивка протестирована и стабильно загружается на **Яндекс Станции Макс с аппаратно заменённым процессором на S905X3**.

---

## О проекте

**AtriOS Linux** — независимый дистрибутив встроенного Linux от **leftymods** для умной колонки **Яндекс Станция Макс** с установленным модом SoC **Amlogic S905X3**.

Проект полностью заменяет проприетарную систему Android 9 на минималистичное и быстрое окружение на базе **Buildroot**, с собственной сборкой драйверов и сервисов.

- **Мгновенный старт**: прямая загрузка в `/sbin/init` (Busybox) за считанные секунды без Android/AOSP, Zygote и фоновых сервисов.
- **Прямой контроль периферии**: драйверы экрана 25×16 Gowin FPGA с поканальной яркостью (0..255), 24-канального RGB-кольца (IS31FL3236A), датчика света, кнопок и энкодера.
- **Сетевой стек**: поддержка двухдиапазонного Wi‑Fi (RTL8822CS) и Bluetooth 5.0 (в отладке) с аппаратным управлением питанием.
- **Единый инструмент управления**: встроенная консольная утилита `atrios` для управления всеми функциями колонки.
- **Самодостаточный репозиторий**: включает полный набор инструментов сборщика (`ampack`, `make_ext4fs`, `dtbTool`), оверлеи и прошивки.

---

## Статус аппаратных компонентов

| Компонент | Железо / Чип | Шина / Интерфейс | Драйвер / Модуль | Статус |
|:---|:---|:---|:---|:---|
| **Загрузчик (U-Boot)** | Native S905X3 Bootloader | eMMC / USB Burn | U-Boot 2015.01 | Работает |
| **UART Консоль** | Amlogic UART0 (115200 8N1) | `/dev/ttyS0` | Интерактивный root shell | Работает |
| **LED Экран (25×16)** | Gowin GW1N FPGA (монохромный) | SPI1 + CS (`GPIOH_7`) | `gowin_led_device` (`/dev/fb4`) | Работает |
| **RGB-кольцо** | 2× IS31FL3236A (24 RGB / 72 LED) | I2C0 (`0x3c`, `0x3f`) | `quasar_led_ring.ko` (`/dev/fb5`) | Работает |
| **Датчик света** | Lite-On LTR-308ALS | I2C0 (`0x53`) | `ltr308als01` (`iio:device0`) | Работает |
| **Крутилка громкости**| Двухфазный квадратурный энкодер | `GPIOH_6`, `GPIOAO_10` | `rotary-volume.ko` | Работает |
| **Верхние кнопки** | Кнопка «Алиса» и Mute | `GPIOA_15`, `GPIOA_14` | `gpio_keypad` (DTS poll 20ms) | Работает |
| **Wi-Fi** | Realtek RTL8822CS (802.11ac 2×2) | SDIO (`sd_emmc_a`) | `88x2es.ko` + `/dev/wifi_power` | Работает |
| **Bluetooth** | Realtek RTL8822CS Bluetooth 5.0 | UART_A (`/dev/ttyAML1`) | `rtk_bt` (`hci0`) | В отладке |
| **HDMI Видеовыход** | MPI7002 / Generic HDMI TX | HDMI TX (`osd1` / `fb0`) | `/dev/fb0` (1024×600 @ 60Hz) | Работает |
| **Зигби (Zigbee)** | Silicon Labs EFR32 | UART_AO_B (`/dev/ttyAML4`) | Reset GPIO (`atrios zigbee`) | В отладке |
| **Микрофонный массив**| Everest ES7210 4-ch ADC | TDM-C, I2C1 | `snd-soc-es7210` (8 каналов) | Не проверено |
| **Динамики** | Silergy SY6045S (20V PVDD) | TDM-B, I2C2 (`0x2a`, `0x2b`) | `snd-soc-sy6045s` | Работает |
| **Аудиовыход 3.5 мм** | Everest ES8156 DAC | TDM-B, I2C2 (`0x08`) | `snd-soc-es8156` | Проверяется |

> [!NOTE]
> **Звук на динамиках работает**: 20V PVDD (`20V_AMPL`), тактирование TDM_B на `mst_b`, SY6045S и маршрутизация ALSA проверены. Микрофонный массив и 3.5-мм выход ещё проходят финальную проверку.

---

## Консольная утилита `atrios`

Все управляющие функции объединены в единый бинарный диспетчер `/usr/bin/atrios`.  
Внутренние модули расположены в `/usr/lib/atrios/` и не засоряют глобальный `PATH`.

```bash
atrios [команда] [параметры]
```

### Доступные команды

| Команда | Описание | Пример использования |
|:---|:---|:---|
| `status` | Состояние сетевых интерфейсов (Wi‑Fi, Bluetooth, Zigbee) | `atrios status` |
| `screen` | Управление матричным экраном 25×16 (Gowin FPGA) | `atrios screen kisser [1\|2\|animate]`<br>`atrios screen eyes [open\|blink\|wink\|happy\|love\|wide\|angry\|sleep]` |
| `ring` | Управление верхним 24-канальным RGB-кольцом | `atrios ring rainbow`<br>`atrios ring volume 75`<br>`atrios ring off` |
| `sensor` | Считывание уровня освещённости (люксы) | `atrios sensor` |
| `input` | Интерактивный монитор нажатий кнопок и энкодера | `atrios input` |
| `wifi` | Сканирование, подключение и питание Wi‑Fi модуля | `atrios wifi scan`<br>`atrios wifi connect "MySSID" "Password"`<br>`atrios wifi power 1` |
| `bt` | Инициализация и сканирование Bluetooth | `atrios bt init`<br>`atrios bt scan` |
| `zigbee` | Сброс чипа EFR32 через GPIO и пинг | `atrios zigbee reset`<br>`atrios zigbee ping` |
| `volume` | Установка уровня системной громкости | `atrios volume 80` |
| `mute` | Отключение / включение микрофонов | `atrios mute on` |
| `amps` | Проверка статуса чипов усилителей | `atrios amps` |
| `test` | Комплексный тест аудиовыхода и микрофонов | `atrios test` |

---

## Системные службы

Фоновые демоны инициализируются при старте системы через `/etc/init.d/S60atrios`:
- **Часы на экране**: `/usr/lib/atrios/screend` — выводит `HH:MM` на матричный экран шрифтом 5×13 с одним двоеточием по центру.
- **Обработка энкодера**: `/usr/lib/atrios/rotaryd` — отслеживает вращение регулятора громкости и нажатия кнопок.
- **Индикация**: отключение микрофона физической кнопкой не блокирует кольцо красным свечением.

Интерактивная командная оболочка:
- `sh` используется в качестве стабильного системного login-shell.
- `~/.profile` безопасно запускает `fish` при наличии поддержки терминала, гарантируя отсутствие артефактов приглашающей оболочки.

---

## Быстрый тест по UART

Подключите переходник USB-UART к контактам консоли (`ttyS0`, 115200 8N1):

```bash
# 1. Проверка загруженных модулей ядра
lsmod

# 2. Тест светодиодного экрана (бегущая строка)
atrios screen ticker "ATRIOS"

# 3. Тест RGB-кольца (радужная анимация)
atrios ring rainbow

# 4. Проверка датчика освещённости
atrios sensor

# 5. Проверка кнопок и кольца регулировки громкости
atrios input

# 6. Проверка сетевых интерфейсов
atrios status

# 7. Полная аудиодиагностика
atrios test
```

---

## Аудио и громкость

- **20V PVDD** — фиксированный регулятор `20V_AMPL`. Он владеет линией GPIOX_10 (line 75, `gpio-485` на этом ядре), поэтому проверять надо состояние регулятора, а не sysfs-файл `gpio485`.
- **Тактирование TDM_B** — на SM1/S905X3 аудиоблок находится по `0xFF660000`, регистры pad-control: `0x20` (MCLK) и `0x24` (SCLK/LRCLK). Записываются значения `0x11` и `0x01110111` соответственно. Адреса `0xFF642000`/`0x1C`/`0x20` из G12A-гайда для S905X3 не подходят.
- **Громкость** — `atrios volume 0..100` и физическая крутилка работают с мастер-контролами `Woofer Master Playback Volume`, `Tweeters Master Playback Volume`, `Headphone DAC Playback Volume`. Поканальные `Ch1/Ch2` остаются на 159 (0 dB).
- **Диагностика** — мастер-громкость и громкость наушников считаются пользовательскими: `atrios test` падает только на 0/mute, а не на конкретном значении 255/193.
- **Крутилка** — `rotaryd` синхронизирует текущую громкость с PipeWire/ALSA, меняет её шагом 2% за детент, а кольцо обновляет сразу, не дожидаясь ALSA/PipeWire (применение громкости вынесено в отдельный worker).
- **Тест музыкой** — положи любой WAV в `/root/music/` (например `/root/music/tsoi.wav`); `atrios test` и `atrios play` возьмут его вместо синуса. В образе есть `/usr/share/sounds/atrios/test-music.wav` (public-domain melody).
- **Диагностика микрофона** — `atrios micprobe` перебирает `Audio In Source` (`LOOPBACK_A/B`, `TDMIN_LB/B`, `PDMIN`) и устройства захвата, печатает RMS/peak/ZCR. По этому выводу видно, какой тракт реально несёт голос, а какой только шумит.

---

## Структура проекта

```
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
└── build_atrios.sh               # Главный скрипт сборки прошивки
```

---

## Сборка прошивки

Сборка полностью автономна: стоковый образ `yandex_1.99.img` **не требуется**.
Все необходимые компоненты лежат в репозитории:

| Компонент | Источник в репозитории |
|---|---|
| Загрузчик (DDR.USB / UBOOT.USB / bootloader.PARTITION) | `device/yandex/station_max/upgrade/bootloader.img` |
| U-Boot для SD-карты (aml_sdc_burn.UBOOT) | `device/yandex/station_max/upgrade/u-boot.bin.sd.bin` |
| Логотип загрузки | `device/yandex/station_max/upgrade/logo.PARTITION` |
| Конфигурация прожига | `device/yandex/station_max/upgrade/{platform.conf,aml_sdc_burn.ini}` |
| Device Tree | `tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts` (компилируется при сборке) |
| Ядро | `tools/fw_builder/overlay/boot/Image.gz` |
| Вендорные модули и firmware | `board/yandex/station2/rootfs_overlay/lib/{modules,firmware}` |
| Корневая ФС | собирается Buildroot (`board/yandex/station2/station2_defconfig`) |

Сборка выполняется в окружении Linux / WSL2:

```bash
cd AtriOS-linux

# Запуск сборки с указанием номера версии (например, v116)
./build_atrios.sh 120
```

Результатом сборки является готовый образ для прошивки:  
`tools/fw_builder/yandex_s905x3_custom_v120.img` (~1.46 ГБ)

Прошивка устройства выполняется через стандартный **Amlogic USB Burning Tool** в режиме восстановления (Recovery) либо через альтернативный загрузочный инструмент, если он используется в вашей сборке.
