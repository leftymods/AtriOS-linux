# `ai/drivers/` — исходники драйверов и низкоуровневых контроллеров устройств

Здесь собраны **все драйверные исходники**, которые реально есть в архивах (прежде всего из
`smart_devices.tar.bz2`), разложенные по подсистемам.

> **Важно понимать уровень.** Это **не** драйверы ядра Linux (нет `.ko`, нет `Kbuild`, нет `module_init`).
> Это **userspace-драйверы и контроллеры устройств**: C++-обёртки над `/dev`, `/sys`, ioctl, tinyalsa,
> SPI/UART/I²C, а также прошивки/стеки микроконтроллеров (EFR32, Telink) и HAL платформы.
> Драйверы ядра (Amlogic BSP: audio, gralloc, hdmi_cec, keymaster, lights, power), U-Boot и Linux
> лежат в **отдельных** репозиториях и в архивах отсутствуют — на них только ссылки в
> `platforms/yandexstation_2/firmware/*/manifest.xml`.

Итого: **3 382 файла, 73 МБ**.

---

## Состав по подсистемам

| Подсистема | Файлов | Что внутри |
|---|---:|---|
| `led/` | 220 | Драйвер LED-экрана Станции 2 (включая foreground-приложения), LED-кольцо, спектрограмма, яркость, цветокоррекция |
| `audio/` | 417 | **Аудиостек Станции 2** (tinyalsa, VQE, RCU, громкость, `audiod`) + **Quasar SDK** (`sdk/`: ALSA, плееры, `mediad`, `audiosender`, `sound_initd`) |
| `gpio/` | 18 | GPIO/кнопки, обработчик кнопок и пульта (RCU) Станции 2 |
| `wireless/` | 2 562 | **Wi-Fi-стек Станции 2** (wifid, wpa_cli, ресетер, прошивки), **Realtek-манифесты** (`realtek/`), Zigbee (EZSP/NCP), Telink, Bluetooth, Gecko Bootloader, вендорские SDK |
| `sensors/` | 6 | ToF-датчик VL53L0X |
| `hal/` | 105 | HAL Станции 2, shared memory, tty, device-id, ключи, PCBA, криптография, DNS, метрики, вендорские библиотеки |
| `station2/` | 53 | **Платформенные находки**: `maind`/`platformd`, `common.cfg` с секретными командами, launch-скрипты, пакеты, эмулятор, метаданные spotter, манифесты прошивок |

---

## `led/` — светодиодные экраны и индикация (211 файлов)

### `led/station2-leds/` (140) — **драйвер LED-экрана Яндекс Станции 2**

Источник: `smart_devices/platforms/yandexstation_2/platformd/leds/`

Ядро драйвера:
- `led_devices_impl.{h,cpp}` — `LedDevicesImpl : LedDevices, YandexStation2LedDevices, YandexIO::BackendConfigObserver, YandexIO::SDKStateObserver`. Управляет списком LED-устройств (`getDevicesList`, `getDefaultDevice`, `getLedScreen`), реакция на конфиг и состояние SDK, часы (`updateClockState`), DND.
- `framebuffer_led_controller.{h,cpp}` — вывод кадров в framebuffer LED-матрицы.
- `yandex_station2_led_devices.h` — интерфейс устройства.
- `frontal_animation/yandex_station_frontal_controller.{h,cpp}` + `cache_manager`, `color_config`, `frame_buffer_output_frame` — контроллер фронтального экрана 25×16.
- `alice_dialog_error_renderer.{h,cpp}` — ошибка диалога Алисы → паттерн `error.led` (3000 мс) + GIF `25x16_Error.gif` ×3.
- `draw_led_screen_directive_observer`, `led_directive_observer` — обработка директив отрисовки.
- `device_state_handler`, `media_state_led_player`, `mics_state_led_player`, `demo_mode_front_led_handler`, `yandex_station2_bluetooth_observer`.

Анимации: `gif_animation`, `repeated_animation`, `compound_animation`, `empty_animation`, `spectrogram_animation`.
Состояния (подпапки): `alarm/`, `clock/`, `configuring/`, `idle/`, `listening/`, `speaking/`, `thinking/`, `timer/`, `update/`, `volume/`, `ring/` (кольцо Станции), `spectrogram/` (bricks/fluid/pads/random/wave + factory).
Утилиты: `utils/` — `gif_utils`, `gif_recorder`, `bgr24`, `draw_utils`, `config_utils`.
Тесты: `tests/` (9 файлов). Утилита: `gifsubst/main.cpp`.

### `led/station2-leds/foreground/` (9) — приложения поверх LED-экрана

`foreground_activity`, `foreground_app_animation`, `app/base/foreground_app` и
**`app/snake_foreground_app`** — настоящая игра «Змейка» на экране 25×16
с управлением с пульта. Это самая неожиданная находка во всей платформе.

> Ресурсы экрана (анимации 25×16, 33 паттерна `.led`, звуки) лежат отдельно в
> `ai/related/smart_devices/platforms/yandexstation_2/data/`.

### `led/segment-clock-leds/` (34) — LED-кольцо других устройств
Источник: `smart_devices/libs/segment_clock_leds/`
`sc_led_controller`, `sc_led_pattern`, `sc_led_frame`, `sc_led_storage`, `sc_led_util`,
анимации `sc_led_clock_animation`, `sc_led_blinking_clock_animation`, `sc_led_timer_countdown_animation`,
спектрограмма `sc_led_spectrogram*`, конвертер `tools/scled_to_bin_converter/`.

### `led/spectrogram/` (7) — расчёт спектрограммы
Источник: `smart_devices/libs/spectrogram/` — `spectrogram.cpp`, `filters.cpp`, `spectrogram_animation`.

### `led/brightness-control/` (17) — яркость и датчик освещённости
Источник: `smart_devices/libs/brightness_control/` — `ambient_light_estimate`, `brightness_animator`.

### `led/color-correction/` (7) — цветокоррекция LED
Источник: `smart_devices/libs/color_correction/` — `led_color_config`.

### `led/segment-clock/` (3) и `led/perlin-noise/` (3)
Источник: `smart_devices/libs/segment_clock/`, `smart_devices/libs/perlin_noise/`.

---

## `audio/` — аудиостек Станции 2 + Quasar SDK (417 файлов)

| Каталог | Файлов | Источник | Что это |
|---|---:|---|---|
| `station2-audio-device/` | 13 | `platformd/audio_device/` | **Аудиоустройство Станции 2**: `YandexStation2AudioDevice`, `AudioReaders`, `TinyAlsaMixer` (loopback/hardware sync), `RcuAudioDevice`, тесты |
| `station2-audio-input/` | 3 | `platformd/audio_input/` | `AudioInputController`: VQE, mute/unmute, ASR/spotter, переключение на пульт |
| `station2-volume-manager/` | 2 | `platformd/` | `YandexStation2VolumeManager`: громкость 2…100, LED-кольцо 105 тиков, AVRCP, будильник |
| `station2-config/` | 2 | `config/common.cfg`, `emulator/` | Аудиосекции `audiod`, `audioclient`, `mediad`, `soundd`, `sound_initd`, `audiosender` и др. |
| `station2-manifests/` | 4 | `firmware/yandexstation_2/{22,88}/manifest.xml` | Драйверы ядра: `tas5825m`, **`sy6045s`**, `tlv320adc3101`, `pcm1774`, `es7210`, `es8156`, `rotary-volume` + история по версиям |
| `station2-platformd-audio-references.txt` | 1 | `platformd.cpp/.h` | Все строки сборки аудио (AudioSource, VQE, AudioDumper, volume) |
| `sdk/` | 391 | `yandex_io/{libs,modules,services,tools,sdk,callkit}` | **Quasar SDK**: ALSA/tinyalsa, плееры, VQE-контроллеры, `mediad`, `audiosender`, `sound_initd`, CallKit-аудио |

Подробный разбор — в [`audio/README.md`](audio/README.md). Ключевое:
8 микрофонов (card 0 / dev 1), 2 динамика (card 0 / dev 0), 48 кГц S16_LE,
VQE preset `yandexstation_2_rev1`; усилители **`tas5825m`** и **`sy6045s`**
(SY6045S — Class-D 2×20 Вт с DSP, появился в прошивке 59), ADC `tlv320adc3101`
/ `es7210`, DAC `pcm1774` / `es8156`.


---

## `gpio/` — GPIO, кнопки, пульт (18 файлов)

- `gpio/gpio/` (10) — источник `libs/gpio/`: `gpio.{h,cpp}`, `button_input.{h,cpp}` — доступ к GPIO и обработка кнопок.
- `gpio/station2-buttons/` (8) — источник `platforms/yandexstation_2/platformd/`: `buttons_handler.{h,cpp}`, `rcu_button.{h,cpp}`, `rcu_button_event_message.{h,cpp}`, `rcu_buttons_controller.{h,cpp}` — кнопки корпуса и пульт ДУ (RCU).

---

## `wireless/` — Wi-Fi, Zigbee, Bluetooth, радиостек (2 562 файла)

### `wireless/wifi/` (49) — **Wi-Fi-подсистема Яндекс.Станции**

Источники: `platforms/yandexstation_2/platformd/firmware_switcher/` и
`yandex_io/{services/wifid,libs/wifi_reseter,libs/wpa_cli,libs/net}`.

| Каталог | Файлов | Что |
|---|---:|---|
| `firmware-switcher/` | 3 | Переключение прошивки Intel 7265D: `22` (`from.4.9`) ↔ `29` backport (`from.backport`), `rmmod/insmod`, `svc wifi`, `reboot` |
| `wifid/` | 20 | Wi-Fi-демон: IPC-сервер, scan, connect, `generate_204`, RSSI/каналы, лимит 10 сетей, NTP после подключения |
| `wifi-reseter/` | 7 | Watchdog: счётчики TX/RX, ошибки, нет интернета, ежедневный сброс, 6 нажатий кнопки, PCI remove/rescan |
| `wpa-cli/` | 4 | `WpaClientSupplicant` — линковка с патченным `libwpa_client` (hostap), `eloop`, monitor-события |
| `wifi-info/` | 5 | Детальная телеметрия через nl80211: BSS, survey, TID, IE, MCS/VHT/HE, rates |
| `firstrund-access-point/` | 3 | Точка доступа `Yandex-Station-Max` через `startap.sh` / `stopap.sh` |
| `realtek/` | 6 | **Realtek Wi-Fi (RTL8xxx)**: манифесты 17 модулей + RTL8822CS, индекс, README. Исходников в архивах нет |

Подробный разбор — в [`wireless/wifi/README.md`](wireless/wifi/README.md).
Ключевой факт: в Станции 2 стоит **Intel Dual Band Wireless-AC 7265D**
(PCIe `0000:01:00.0`), а не Broadcom/Realtek.

### `wireless/wifi/realtek/` — Realtek (rtw / RTL8xxx)

Отдельная папка со всем, что относится к Realtek Wi-Fi:

- `manifests/station2-22.xml.fragment`, `station2-88.xml.fragment` — 17 модулей
  (`8188eu`, `8188ftv`, `8189es`, `8189ftv`, `8192cu/du/es/eu`, `8723au/bs/bu/ds/du`,
  `8811au`, `8812au`, `8822bs`, `8822bu`);
- `manifests/station-plus-buildroot-master.xml.fragment` — `8822cs`,
  `yandex-io/buildroot-external-realtek`, `bt_uart_8822cs`;
- `realtek-manifest-index.txt` — все 89 манифестов с Realtek, сгруппированные по совпадению;
- `README.md` — таблица чипов/шин/ревизий и команды `repo sync`.

**Исходников драйверов Realtek в архивах нет** — только записи в manifest.xml
(имена репозиториев, пути, revision). Подробности — в
[`wireless/wifi/realtek/README.md`](wireless/wifi/realtek/README.md).

### Остальное

| Каталог | Файлов | Источник | Что это |
|---|---:|---|---|
| `zigbee/` | 125 | `libs/zigbee/` | **Zigbee-стек Yandex**: `endpoint`, `ezsp`, `ncp` — работа с EZSP-координатором |
| `telink/` | 66 | `libs/telink/` | Telink: `bootloader`, `hci`, `mfg_test`, `swire` |
| `bluetooth/` | 12 | `libs/bluetooth/` | Bluetooth-адаптеры: `linkplay`, `mt8516` (Amlogic) |
| `gecko-bootloader/` | 5 | `libs/gecko_bootloader/` | Gecko Bootloader: `gbl.{h,cpp}` (формат GBL), `reader` |
| `iot-tuya-smartlink/` | 17 | `libs/iot/` | IoT-возможности + `tuya_smart_link` |
| `third-party-gecko-sdk/` | 1 742 | `third_party/gecko_sdk/` | Silicon Labs Gecko SDK (EFR32): HAL, bootloader, radio |
| `third-party-tl-zigbee-sdk/` | 540 | `third_party/tl_zigbee_sdk/` | Telink Zigbee SDK: `apps/`, `platform/`, `proj/`, `apps/bootLoader/` |
| `third-party-linkplay-bluetooth/` | 3 | `third_party/linkplay_bluetooth/` | Интерфейс BT Linkplay + заглушки |
| `third-party-tuya-sdk/` | 3 | `third_party/tuya_sdk/` | Tuya Smart Link |

---

## `sensors/` — датчики (6 файлов)

- `sensors/vl53l0x/` — источник `third_party/vl53l0x/`: Time-of-Flight дальномер VL53L0X (`defs.h`, `vl53l0x_def.h`, `vl53l0x_device.h`, `vl53l0x_types.h`). Используется в устройствах с датчиком приближения.

---

## `hal/` — HAL и низкоуровневые сервисы (105 файлов)

| Каталог | Файлов | Источник | Что это |
|---|---:|---|---|
| `station2-device/` | 5 | `platforms/yandexstation_2/device/` | **HAL Станции 2**: `yandex_station2_hal.{h,cpp}`, `device.cpp` |
| `shared-memory/` | 3 | `libs/shared_memory/` | Разделяемая память между процессами |
| `tty/` | 3 | `libs/tty/` | Работа с последовательными портами |
| `device-id/` | 10 | `libs/device_id/` | Идентификатор устройства: `android_property`, `cmdline_serialno`, `linkplay` |
| `platform-key/` | 8 | `libs/platform_key/` | Ключи платформы, `digest`, `generate_secret_source.py` |
| `pcba/` | 5 | `libs/pcba/` | Идентификаторы PCBA |
| `device-cryptography/` | 4 | `libs/device_cryptography/` | Криптография устройства (plain-file вариант для эмулятора) |
| `dns/` | 12 | `libs/dns/` | `dhcpcd_dns_updater`, `dns_health_checker` |
| `unbound/` | 5 | `libs/unbound/` | Включение локального DNS-резолвера Unbound |
| `files-content-monitor/` | 5 | `libs/files_content_monitor/` | Слежение за содержимым файлов |
| `metrics-collector/` | 3 | `libs/metrics_collector/` | Сбор метрик |
| `metrics/` | 2 | `platforms/yandexstation_2/platformd/metrics_collector_impl.*` | Реализация сборщика метрик Станции 2 |
| `volume-manager/` | 2 | `platforms/yandexstation_2/platformd/yandex_station2_volume_manager.*` | Менеджер громкости Станции 2 (105 градаций) |
| `drm-keys-reworker/` | 3 | `platforms/yandexstation_2/platformd/drm_keys_reworker/` | Widevine + HDCP 1.4/2.2 через `tee_provision` |
| `third-party-mediatek/` | 15 | `third_party/mediatek/` | Amlogic/MediaTek MT8516: обёртки и заголовки |
| `third-party-linkplay/` | 15 | `third_party/linkplay/` | Linkplay: NVRAM, resample, soft-prep |
| `third-party-xmodem/` | 5 | `third_party/xmodem/` | XMODEM-протокол (`xmodem.c`, `crc16.c`) — прошивка по UART |

> Wi-Fi-переключатель прошивок перенесён из `hal/` в
> `wireless/wifi/firmware-switcher/` — это его правильное место.

---

## `station2/` — платформенные находки (53 файла)

Не драйверы, а самое интересное из полного разбора платформы
`related/smart_devices/platforms/yandexstation_2/`:

| Каталог | Файлов | Что |
|---|---:|---|
| `core/` | 4 | `maind.cpp` (8 демонов в одном бинаре), `platformd.cpp` (535 строк), `ya.make` |
| `config/` | 2 | `common.cfg` — аудио, aliced, **6 секретных команд setupd**, monitord, BLE, calld; `dev_overlay.cfg` |
| `launch/` | 11 | `launch.json`, `onboot.sh`, `run.sh`, `activate_adb.sh`, `device_ota_update.sh`, `ntp_sync.sh` и др. |
| `packages/` | 5 | Релизный пакет: `VERSION 3.21`, daemons/data/install_root, символы |
| `emulator/` | 4 | `maind` эмулятора + overlay с TCP-звуком и **открытыми `client_id`/`client_secret`** |
| `spotter-meta/` | 20 | Метаданные моделей активации: `words.txt`, `flags.txt`, TTS-тайминги, `counteractivation-features` |
| `firmware/` | 6 | Repo-манифест прошивки 88, buildroot-манифест (Broadcom AP6xxx, Realtek 8822CS, драйверы `quasar-led-*`) |

Подробности — в [`station2/README.md`](station2/README.md).

---

## Чего здесь НЕТ (и где искать)

| Отсутствует | Где на самом деле |
|---|---|
| Драйверы ядра Linux, `*.ko`, `Kconfig`, `*_defconfig` | репозиторий `unity/kernel/common` (ветка `yandexstation_2/88`), `unity/kernel/configs` |
| Device Tree (`*.dts`, `*.dtsi`, `*.dtb`) | там же (`unity/kernel/common`), в архивах — **0 файлов** |
| U-Boot, `bl2/bl30/bl31/bl32` | `unity/uboot`, `unity/firmware/bin/bl*` |
| Amlogic BSP / HAL (audio, gralloc, hdmi_cec, keymaster, lights, power) | `unity/p-amlogic/platform/hardware/amlogic/*` |
| Прошивка FPGA LED-экрана (Verilog) | `hardware-stplus-display-verilog` → `vendor/yandex/src/firmwares/led_screen_fpga` |
| Исходники `iwlwifi` / `iwlmvm` / `mac80211` | `yandex-io/kernel/drivers/backport-iwlwifi` |
| **Исходники Realtek Wi-Fi** (`8188eu`, `8192eu`, `8822cs`, …) | `unity/platform/hardware/wifi/realtek/drivers/*`; см. [`wireless/wifi/realtek/README.md`](wireless/wifi/realtek/README.md) |
| ASoC-драйверы (`tas5825m`, `sy6045s`, `es7210`, `es8156`, `tlv320adc3101`, `pcm1774`, `rotary-volume`) | `yandex-io/kernel/drivers/*`, ядро Amlogic |
| Прошивки `iwlwifi-7265D-22/29.ucode`, `*.ko` Wi-Fi | `/vendor/lib/modules`, `/vendor/lib/backported_iwlwifi` на устройстве |
| `startap.sh` / `stopap.sh` / `wireless_iface_up.sh` | Android/buildroot-образ (`/system/bin`, `/system/vendor/quasar`) |
| Quasar SDK `yandex_io` (`LedManager`, `LedDevices`, `SDKStateObserver`) | **теперь есть** в `ai/related/yandex_io/` (3 722 файла) |

Все эти репозитории перечислены в `ai/related/smart_devices/platforms/yandexstation_2/firmware/*/manifest.xml`
(remote `gerrit.yandex-team.ru`, `bb.yandex-team.ru/quas`). Чтобы получить их — нужен доступ и `repo sync`.

---

## Как это скопировано

Два скрипта:

- `/tmp/opencode/move_drivers.sh` (лог `/tmp/opencode/drivers.log`) — первичная
  раскладка по подсистемам из `smart_devices.tar.bz2`;
- `/tmp/opencode/copy_findings.sh` — Wi-Fi-стек из `yandex_io` и платформенные
  находки Станции 2 в `station2/`.

Файлы **скопированы** (не перемещены). Источники сохранены в
`ai/related/smart_devices/` и `ai/related/yandex_io/` — оттуда можно посмотреть
файл в исходном контексте.
