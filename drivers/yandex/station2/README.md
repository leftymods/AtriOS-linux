# `ai/drivers/station2/` — интересные находки по платформе Яндекс.Станция Макс

Сюда вынесены **не драйверы, а платформенные артефакты**, которые оказались
самыми интересными при полном разборе
`related/smart_devices/platforms/yandexstation_2/` (570 файлов, 13 750 строк C++).

Сами драйверы Станции 2 уже разложены по подсистемам:

- LED (экран 25×16, кольцо 24, змейка) → `ai/drivers/led/station2-leds/`
- аудио (8 микрофонов, tinyalsa, VQE) → `ai/drivers/audio/station2-audio-device/`
- кнопки и пульт → `ai/drivers/gpio/station2-buttons/`
- Wi-Fi → `ai/drivers/wireless/wifi/`
- DRM-ключи → `ai/drivers/hal/drm-keys-reworker/`
- метрики и громкость → `ai/drivers/hal/metrics/`, `hal/volume-manager/`
- HAL → `ai/drivers/hal/station2-device/`

---

## Состав (53 файла, 492 КБ)

| Каталог | Файлов | Что |
|---|---:|---|
| `core/` | 4 | `maind.cpp`, `platformd.{h,cpp}`, `ya.make` — точка входа и сборка демона платформы |
| `config/` | 2 | `common.cfg` (главный конфиг) и `dev_overlay.cfg` |
| `launch/` | 11 | `launch.json` + все shell-скрипты запуска и обслуживания |
| `packages/` | 5 | Описание релизного пакета (`VERSION 3.21`) |
| `emulator/` | 4 | `maind.cpp` эмулятора + `emulator_overlay.cfg` |
| `spotter-meta/` | 20 | Текстовые метаданные моделей активации (без `.nnet`/`.mat`) |
| `firmware/` | 6 | repo-манифесты: последняя прошивка, buildroot, branch |

---

## 1. `core/` — как устроен `maind`

`maind` — **один бинарь-мультиколл** на 8 демонов:

```
maind --yiod            maind --updatesd      maind --mediad
maind --glagold         maind --yandexstation_2   ← platformd
maind --telemetry       maind --fluent-bitd   maind --calld
```

Плюс симлинки-утилиты: `quasar_client`, `led_util`, `keymaster_proxy_app`
(все → `maind`).

`platformd.cpp` (535 строк) инициализирует: SDK, LED-менеджер и яркость,
аудиовход и эквалайзер, spotter, геолокацию, громкость, кнопки и пульт,
Bluetooth, demo-режим, IoT (Tuya), персонализацию (voice-id),
**переключатель Wi-Fi прошивки**, DRM-ключи, testpoint, Xiva push.

---

## 2. `config/common.cfg` — самое ценное в одном файле

Внутри:

- **Аудио**: mic `card 0/dev 1` (8 каналов), spk `card 0/dev 0` (2),
  loopback `card 1/dev 1`, HDMI loopback `card 1/dev 0`; 48 кГц S16_LE,
  `periodSize 3072 × 4`; VQE preset `yandexstation_2_rev1`, `omniMode`.
- **aliced**: 15 экспериментов, **40 supportedFeatures** (multiroom,
  stereo_pair, led_display, clock_display, calls, muzpult, кодеки,
  equalizer), `memoryLimitRssKB: 85000`.
- **`setupd.secretMsgList`** — 6 заводских команд по SHA-1:
  - `activate_adb.sh --sound activate` — **включить ADB**;
  - проиграть `qc_sweep.wav`;
  - `unlock_bricked_bt` — разблокировать Bluetooth;
  - kill `yandexstation_2` + `led-show check_led.bmp > /dev/graphics/fb4`;
  - `startap.sh Yandex-Station-Max-<serial>` — **точка доступа**;
  - `demo_mode`.
- **monitord**: `i2c-1`, список сервисов и диск `mmcblk0`.
- **BLE**: `manufacturerId 2309`, кастомные UUID.
- **calld**: порт 9908, 32 кГц.

Полный файл лежит рядом: `station2/config/common.cfg`.

---

## 3. `launch/` — скрипты жизненного цикла

| Файл | Что делает |
|---|---|
| `launch.json` | Описание запуска демонов: `affinity [1,2,3]`, `delay 5`, `allowedRestartIntervalSeconds 600` |
| `onboot.sh` | Первый запуск: копирование конфига, инициализация |
| `run.sh` | `ulimit -n 10240`, `oom_score_adj=0`, `SQLITE_TMPDIR=/tmp` — «иначе деградирует eMMC» |
| `gen_keys.sh` | Генерация ключей устройства при первом старте |
| `ntp_sync.sh` | Циклическая синхронизация времени |
| `set_monotonic_time.sh` | Монотонное время |
| `activate_adb.sh` | **Включение ADB** по скрытой команде |
| `device_ota_update.sh` | Запись `--update_package` в `/cache/recovery/command` |
| `mem_monitor.sh` | Дамп памяти из debugfs |
| `performance_test.sh` | Замер температуры/производительности |
| `play_pattern.sh` | Проигрывание LED-паттерна |

---

## 4. `packages/` — релизный пакет

| Файл | Что |
|---|---|
| `daemons.json` | `quasar_daemons_yandexstation_2`, maintainer `quasar-team@yandex-team.ru` |
| `data.json` | `quasar_data_yandexstation_2`, **`VERSION = 3.21`**: конфиг, CA-сертификаты, `ota_script.pub`, звуки, модели |
| `install_root.json` | `maind`, `quasar_launcher2`, `updater_switcher`, `libs/`, симлинки |
| `symbols.json` | Символы для символизации крэшей |
| `ya.make` | Сборка пакета |

Флаги: `android-armv8a`, `PIE`, `QUASAR_RUNPATH=/system/vendor/quasar/libs`,
динамические `AVAHI/ICONV/GSTREAMER/SPOTTER/VQE/OPENAL`.

---

## 5. `emulator/` — эмулятор платформы

`emulator_overlay.cfg`:

```json
"audiod": { "audioDevice": { "device": "tcp", "bind": "0.0.0.0", "port": 10000 },
            "defaultVqeType": "no", "brokenMicDetector": { "enabled": false } },
"common": { "deviceType": "development",
            "cryptography": { "cryptographyType": "plainFile", ... } },
"emulator": { "passport": { "client_id": "...", "client_secret": "..." } }
```

Важно:

- звук уходит по **TCP :10000**;
- VQE отключён;
- криптография — plain-file;
- **в открытом виде лежат `client_id`/`client_secret`** (TUS/Passport),
  `uid: 1427288149`, `tus_consumer: yandex_tv`, `env: prod`.

`emulator/maind.cpp` поднимает 6 демонов (без `fluent-bitd` и `calld`).

---

## 6. `spotter-meta/` — метаданные моделей активации

Скопированы только текстовые файлы (`.nnet`/`.mat` — бинарные модели,
остались в `related/smart_devices/.../spotter_models/`):

| Модель | Файлы |
|---|---|
| `activation/alisa` | `flags.txt`, `words.txt`, `lda`-фичи, `tts-timings-info/{phones,rules,words}.txt`, `counteractivation-features/flags.txt` |
| `activation/yandex` | то же для слова «Яндекс» |
| `general`, `stop`, `navi`, `navi_old`, `music`, `video` | `flags.txt` (+ `words.txt` у `navi_old`) |

Самое интересное: **`counteractivation-features`** — отдельная модель
анти-ложной активации, и **`tts-timings-info`** — тайминги TTS для barge-in.

---

## 7. `firmware/` — манифесты

| Файл | Что |
|---|---|
| `yandexstation_2-88/manifest.xml` | Последняя версия прошивки: repo-манифест со всеми проектами |
| `repository.json` | `ref: yandexstation_2/88`, tracked branches 84–88 |
| `branch_yandexstation_plus_develop_manifest.xml` | Ветка `yandexstation_plus_develop` |
| `buildroot_firmware/master/manifest.xml` | **Buildroot-манифест Station Plus**: Broadcom AP6xxx, Realtek 8822CS, драйверы `quasar-led-*`, `tas5825m`, `vl53l0x`… |
| `buildroot_firmware/repository.json` | `yandex-io/buildroot-manifest`, ref `master` |

Именно отсюда видно, что исходников ядра/U-Boot/FPGA в архиве нет — только
ссылки на `gerrit.yandex-team.ru` и `bb.yandex-team.ru/quas`.

---

## 8. Где лежат остальные находки

| Находка | Путь |
|---|---|
| LED-экран 25×16, кольцо 24, 5 спектрограмм, часы | `ai/drivers/led/station2-leds/` |
| **Змейка на LED-экране** | `ai/drivers/led/station2-leds/foreground/app/snake_foreground_app.cpp` |
| 8 микрофонов, reorder map, 6 beamforming | `ai/drivers/audio/station2-audio-device/audio_readers.cpp` |
| tinyalsa-микшер (Loopback Enable, TDMIN/TDMOUT) | `ai/drivers/audio/station2-audio-device/tinyalsa_mixer.*` |
| Кнопки, 6 нажатий → Wi-Fi reset, пульт | `ai/drivers/gpio/station2-buttons/` |
| Wi-Fi 22↔29, wifid, ресетер, AP | `ai/drivers/wireless/wifi/` |
| DRM: Widevine + HDCP 1.4/2.2 через `tee_provision` | `ai/drivers/hal/drm-keys-reworker/` |
| Метрики: CPU, eMMC, LED, Wi-Fi temperature | `ai/drivers/hal/metrics/` |
| Громкость: 105 градаций, Android 2–100 | `ai/drivers/hal/volume-manager/` |
| HAL: BLE-only, keymasterProxy, Tuya discovery | `ai/drivers/hal/station2-device/` |

---

## 9. Что осталось в `related/smart_devices/` (не копировалось)

- 67 версий прошивки (`firmware/yandexstation_2/22…88`) — 156 КБ только
  последняя; остальные отличаются в основном ревизиями проектов;
- 52 звука (`data/sounds/`, ~30 МБ);
- 97 GIF 25×16 и 33 `.led`-паттерна (`data/`, ~10 МБ);
- бинарные модели spotter (`.nnet`, `.mat`, ~9 МБ);
- `data/ntpclient_aarch64` и `prebuilt_libs/libopenal.so.1`.

Всё это доступно по исходному пути:
`ai/related/smart_devices/platforms/yandexstation_2/`.
