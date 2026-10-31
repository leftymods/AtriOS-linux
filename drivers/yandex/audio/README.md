# `ai/drivers/audio/` — аудиостек Яндекс.Станции 2 + Quasar SDK

Два слоя:

- **`station2-*`** — платформенный аудиокод именно Станции 2 (`platformd`);
- **`sdk/`** — Quasar SDK (`yandex_io`), который этот код использует: ALSA/tinyalsa,
  плееры, VQE-контроллеры, `mediad`, `audiosender`, `sound_initd`.

**417 файлов, 9.5 МБ.** Источники: `smart_devices/platforms/yandexstation_2/`
и `yandex_io/`.

---

## Состав

| Путь | Файлов | Что это |
|---|---:|---|
| `station2-audio-device/` | 13 | Аудиоустройство Станции 2: `YandexStation2AudioDevice`, `AudioReaders`, `TinyAlsaMixer`, `RcuAudioDevice` + тесты |
| `station2-audio-input/` | 3 | `AudioInputController` — VQE, mute/unmute, ASR/spotter, переключение на пульт |
| `station2-volume-manager/` | 2 | `YandexStation2VolumeManager` — громкость, LED-кольцо, AVRCP, будильник |
| `station2-config/` | 2 | Аудиосекции `common.cfg` и `emulator_overlay.cfg` |
| `station2-manifests/` | 4 | Драйверы ядра из прошивок 22 и 88, история по версиям, diff 59 ↔ 88 |
| `station2-platformd-audio-references.txt` | 1 | Все строки `platformd.cpp/.h`, где собирается аудио |
| **`sdk/`** | **391** | **Quasar SDK: весь аудиостек `yandex_io`** |

### `sdk/` подробно

| Путь | Файлов | Что внутри |
|---|---:|---|
| `sdk/libs/` | 71 | `audio/` — ALSA + tinyalsa чтение/запись; `audio-player/` — ALSA/GStreamer-плеер; `speechkit-audio-player/` |
| `sdk/input/` | 167 | `audio-input/` — микрофонный тракт, VQE-контроллеры, multiple VQE, dumper; `volume-manager/` — реализация громкости |
| `sdk/services/` | 115 | `mediad/` (аудиоклиент, аудиочасы, спектр), `audiosender/`, `sound-initd/` |
| `sdk/tools/` | 11 | `audio-lag-finder/`, `soundinit-generator/` |
| `sdk/misc/` | 27 | звук настройки, sound-init для LED, CallKit-аудио, sample app, `AudioSource` |

---

## Железо Станции 2

### Параметры `audiod` (из `common.cfg`)

| Параметр | Значение |
|---|---|
| Микрофоны | 8 каналов, card 0 / device 1, 48 кГц, S16_LE |
| Динамики | 2 канала, card 0 / device 0, 48 кГц |
| Loopback | card 1 / device 1 (2 канала) |
| HDMI loopback | card 1 / device 0 |
| Период | 3072 сэмпла × 4 периода |
| VQE по умолчанию | `yandex`, preset **`yandexstation_2_rev1`**, `spkChannels: 6`, `omniMode: true` |
| Основной канал | `vqe` |
| Дамп аудио | `/mnt/` (`dumpPath`) |
| Broken mic detector | включён |

### Аудиочипы (драйверы ядра, в архивах только манифесты)

| Чип | Роль | Шина | С какой версии |
|---|---|---|---|
| `tas5825m` | Class-D усилитель с DSP (TI) | I²C + I²S | с 22 |
| **`sy6045s`** | **Class-D усилитель 2×20 Вт с DSP (Silergy)** | I²C + I²S | **с 59** |
| `tlv320adc3101` | ADC (TI) | I²C + I²S | с 22 |
| `pcm1774` | DAC (TI) | I²C + I²S | с 22 |
| `es7210` | 4-канальный ADC (Everest) | I²C + I²S | с 59 |
| `es8156` | DAC (Everest) | I²C + I²S | с 59 |
| `rotary-volume` | энкодер громкости | GPIO | с 22 |
| Amlogic `hardware/amlogic/audio` | платформенный audio HAL | — | с 22 |
| `libaudioeffect` | аудиоэффекты Amlogic | — | с 22 |

Детали и revision по версиям — в
[`station2-manifests/driver-history.md`](station2-manifests/driver-history.md).

Важное уточнение: **`sy6045s` — не «ещё одна шина питания», а второй
аудиоусилитель**. SY6045B/S — цифровой Class-D 2×20 Вт: I²S slave, I²C,
встроенный 32-битный DSP (PEQ, loudness, DRC, hard clipper), PVDD 4.5–28 В,
2.0 BTL или PBTL. Появился в прошивке 59; в 22–58 его нет.

---

## Код Станции 2

### `station2-audio-device/`

- **`yandex_station2_audio_device.{h,cpp}`** — `YandexStation2AudioDevice :
  YandexVqeAudioDeviceBase`: захват звука, `setSpotterMode()` / `setASRMode()`,
  `getDOAAngle()` (направление на говорящего), привязка capture-потока к CPU,
  явный старт/стоп tinyalsa.
- **`audio_readers.{h,cpp}`** — ридеры: микрофоны, loopback, HDMI loopback.
- **`tinyalsa_mixer.{h,cpp}`** — `TinyAlsaMixer`: `enableHardwareSync()`,
  `disableHardwareSync()`, `setLoopbackSource(FeedbackSource)`.
- **`rcu_audio_device.{h,cpp}`** — аудио с пульта (RCU) как отдельный источник.

### `station2-audio-input/`

`AudioInputController : IMultipleVqeController, BackendConfigObserver`:
`setASRMode()` / `setSpotterMode()`, `mute()` / `unmute()` (пульт не глушится),
`setActiveVqe()`, `onSystemConfig()`, переключение на вход пульта по тегу.

### `station2-volume-manager/`

`YandexStation2VolumeManager : VolumeManager`: диапазон 2…100, LED-кольцо
**105 тиков**, маппинг в шкалу Алисы, `playVolumeLed()`, AVRCP, громкость
будильника.

Зависимости (`VolumeConductor`, `RcuVolumeConductor`, анимации кольца) —
в `ai/drivers/led/station2-leds/volume/`.

---

## Конфиг

`station2-config/common.cfg.audio.json` — секции:

| Секция | Что задаёт |
|---|---|
| `audiod` | карты/устройства ALSA, VQE, loopback, дамп, broken mic |
| `audioclient` | GStreamer-пайплайны (`openslessink`, `equalizer-nbands`, `spectrum`) |
| `mediad` | медиасервис |
| `multiroomd` | мультирум-аудио |
| `soundd` | системные звуки |
| `sound_initd` | настройка звуком |
| `audiosender` | отправка аудио на внешние устройства |
| `spectrogram_animation` | спектр для LED |
| `led_screen`, `personalization` | связка звука с экраном/персонализацией |

`emulator_overlay.cfg.audio.json` — эмулятор: `useAudioClientTtsPlayer: true`,
`audioDevice`, `defaultVqeType: "no"`.

---

## Прошивка 59 и TDM

**Прошивки 59 в архивах нет** — только `manifest.xml` (1 файл, 156 КБ) со
списком репозиториев и revision. Ни образа, ни `.ko`, ни DTS, ни драйверов.

Версия 59 интересна тем, что именно в ней появились `es7210`, `es8156` и
`sy6045s`. По сравнению с 88 изменились только:

| Репозиторий | 59 | 88 |
|---|---|---|
| `yandex-io/kernel/drivers/tas5825m` | `008e1eb0` | `e6f38533` |
| `yandex-io/kernel/drivers/sy6045s` | `44550212` | `92cccf17` |
| `yandex-io/kernel/drivers/es7210` | `d87f8e09` | `a9835172` |
| `unity/p-amlogic/platform/hardware/amlogic/audio` | `44e4416d` | `83824590` |

`es8156`, `pcm1774`, `tlv320adc3101`, `rotary-volume` не менялись.
Подробная таблица — в
[`station2-manifests/firmware-59-vs-88-audio-diff.md`](station2-manifests/firmware-59-vs-88-audio-diff.md).

**Про TDM:** в архивах нет ни одного упоминания `tdm` — ни в конфигах, ни в
коде, ни в манифестах. TDM задаётся в DTS ядра (`unity/kernel/common`),
Amlogic audio HAL и драйверах кодеков; всего этого в архивах нет. То есть
повторить TDM из 59 в новых прошивках по этим файлам невозможно — нужны
исходники ядра и HAL обеих веток.

---

## Чего здесь нет

| Отсутствует | Где искать |
|---|---|
| Драйверы ядра `tas5825m`, `sy6045s`, `tlv320adc3101`, `pcm1774`, `es7210`, `es8156` | `yandex-io/kernel/drivers/*` — только ссылки в `station2-manifests/` |
| Amlogic audio HAL | `unity/p-amlogic/platform/hardware/amlogic/audio` — только манифест |
| `audio_policy.conf`, `mixer_paths.xml` | Android-образ, `/vendor/etc/` |
| `.ko`-модули, firmware DSP | образ устройства, `/vendor/lib/modules/` |
| Звуковые ресурсы (50 `.wav`/`.mp3`, ~30 МБ) | `ai/related/smart_devices/platforms/yandexstation_2/data/sounds/` |

---

## Как это связано

```
8 микрофонов (card0/dev1) ──> AudioReaders ──> YandexStation2AudioDevice
                                   │                    │
                                   │            AudioInputController
                                   │             (VQE / mute / ASR)
                                   │
2 динамика (card0/dev0) <── TinyAlsaMixer <── audio_player (SDK)
                                   │
                     TAS5825M и/или SY6045S (I²C + I²S, драйверы ядра)

пульт (RCU) ──> RcuAudioDevice ──> AudioInputController (переключение по тегу)

громкость ──> YandexStation2VolumeManager ──> LED-кольцо (105 тиков)
```
