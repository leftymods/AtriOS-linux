# AtriOS Nebula

**AtriOS Nebula** — открытый мульти-колоночный Linux-фреймворк и дистрибутив на базе **Mainline Linux 7.x (`AtriOS-aml`)**, **Mainline U-Boot** и **zero-Python Rust + C userspace** для умных колонок и встраиваемых аудиоплатформ от **leftymods**.

Фреймворк спроектирован так, чтобы поддерживать несколько моделей умных колонок из одной кодовой базы: профили плат и семейств (`config/boards/*.conf`, `config/families/*.conf`, `config/bootloaders/*.conf`), аппаратный манифест возможностей (`/etc/atrios/hardware.json`), 13 перенесённых на mainline-ядро драйверов (`drivers/atrios/`), единый мульти-апплетный Rust-бинарник `atrios-runtime` (1.5 МБ), 7-микрофонный VQE/AEC на C, автономный Bluetooth A2DP-приёмник, оффлайн-мини-ИИ на русском, прямую интеграцию с Яндекс Музыкой, отказоустойчивое OTA с двухфазным откатом и мобильное приложение **AtriOS Control v1.6**.

* **Поддерживаемые устройства и портирование новых плат:** [DEVICES.md](DEVICES.md)
* **Статус драйверов ядра (`drivers/atrios/`):** [DRIVERS.md](DRIVERS.md)
* **История изменений:** [CHANGELOG.md](CHANGELOG.md)

---

## Ключевые возможности

### 1. Мульти-колоночная архитектура и аппаратный манифест
* **Изолированные профили сборки:** `config/boards/*.conf`, `config/families/*.conf`, `config/bootloaders/*.conf` и изолированные кеши ядра `lib/kernel-profile.py` гарантируют, что артефакты разных колонок никогда не смешиваются.
* **Аппаратный манифест (`/etc/atrios/hardware.json`):** генерируется при сборке из профиля платы (дисплей, число светодиодов кольца, I2C-адреса усилителей, число микрофонов, UART Bluetooth/Zigbee, флаги `rotary`/`als`/`zigbee`/`bluetooth`) и считывается рантаймом (`atrios-runtime hardware`).
* **Zero-Python на колонке:** весь userspace написан на Rust (`atrios-runtime`, 15 апплетов через симлинки на один 1.5 МБ бинарник) и C (`atri-aec` 14 КБ, `bt-speaker-daemon` 34 КБ). Rootfs-оверлей очищен со 180 МБ до **23 МБ**.
* **Супервизор сервисов (`supervise`):** встроенный апплет `atrios-runtime supervise` с экспоненциальным backoff (1–30 с), атомарным `/var/run/atrios/health.json` и опциональной поддержкой `/dev/watchdog`.

### 2. Оффлайн мини-ИИ на русском + удалённый GPU AtriAI / Qwen3-TTS
* **Локальный VQE + AEC (`atri-aec`):** 7-микрофонный PDM-захват (48 кГц → 16 кГц), 4-го порядка HPF 192 Гц, VQE MicChooser, NLMS эхоподавление с защитой от double-talk и подавление стационарного шума.
* **Локальное распознавание и интенты (`local_stt.rs`, `local_ai.rs`):** Vosk Russian small с конечным автоматом грамматики (`CMD_GRAMMAR`) для мгновенной оффлайн-реакции без интернета: таймеры, будильники, текущее время, управление громкостью, подсветкой, Bluetooth и музыкой.
* **Удалённый GPU-ассистент (`atriai.rs` + `https.rs`):** HTTPS-клиент (`/asr`, `/chat`, `/tts`) с поддержкой **Qwen3-TTS** (9 русских эмоций: `neutral`, `happy`, `sad`, `serious`, `whisper`, `shout`, `angry`, `excited`, `surprised`), живым прерыванием (barge-in) и автоматическим приглушением музыки (ducking) при активации.

### 3. Прямая Яндекс Музыка (`ymusic`)
* Воспроизведение напрямую с колонки без телефона по OAuth-токену (`/var/lib/atrios/ymusic.json`, `0600`): **Моя волна**, **Любимые треки**, поиск по названию/исполнителю, **Лайк / Дизлайк**, пауза/продолжить, следующий/предыдущий трек, автопереход и озвучивание текущего трека.
* Управление как голосом, так и из приложения **AtriOS Control v1.6** (включая OAuth-вход через телефон).

### 4. Bluetooth A2DP-колонка без заиканий и динамический лимитер баса
* **Автономный `bt-speaker-daemon` (34 КБ, C):** BR/EDR SDP + A2DP Sink (SBC → 48 кГц) + AVRCP с двусторонней синхронизацией громкости и передачей референса в `atri-aec`.
* **Защита от периодических заиканий A2DP:**
  * Приоритет процесса `nice -10` (`setpriority`), буфер L2CAP-сокета `SO_RCVBUF = 256 КБ`.
  * Буфер ALSA увеличен до **350 мс** + **120 мс jitter pre-buffer** на старте потока и после underrun.
  * Автоматическое отключение HCI Inquiry/Page Scan (`HCI_SCAN_DISABLED`) на время активного воспроизведения A2DP и возврат `PSCAN`/`PISCAN` при паузе/остановке.
  * Отключение энергосбережения Wi-Fi (`iw dev wlan0 set power_save off`) для чистого 2.4 ГГц Wi-Fi/BT coexistence.
* **Динамический лимитер баса:** выше 40% громкости вуфер (SY6045S `0x2b`) плавно ограничивается относительно твитеров (`0x2a`) в `rotaryd`, `bt-volume-sync` и `atrios volume`, предотвращая перегрузку и хрип на высокой громкости.

### 5. Отказоустойчивое OTA с двухфазным автоматическим откатом
* **Неблокирующие фоновые проверки (`S85atrios-update`):** низкий приоритет (`nice(10)`), 90 с пауза после загрузки, тихое ожидание Wi-Fi и экспоненциальный backoff — временная ошибка сети/DNS никогда не тормозит систему и не перехватывает экран часов.
* **Криптографическая проверка и двухфазный откат (`ota.rs`):** проверка RSA/SHA-256 подписи `manifest.sig` по `/etc/atrios/ota-public.pem`; перед перезаписью всех файлов создаются и верифицируются по SHA-256 резервные копии; при любой ошибке на этапе распаковки, записи или пост-проверки выполняется автоматический откат (`rollback_transaction_at`) + восстановление после сбоя питания через `S04atrios-ota-recover`.
* **Управление с телефона:** проверка, статус, changelog, прогресс, запуск обновления и ручной откат доступны из Android-приложения по Bluetooth SPP.

### 6. Приложение-компаньон Android (**AtriOS Control v1.6**)
Исходный код приложения размещён в отдельной ветке [`app`](../../tree/app) (`app-v1.6`):
* Поддержка нескольких сохранённых колонок и автоопределение возможностей модели (`DeviceCapabilities`).
* Проверка и установка OTA-обновлений с прогресс-баром, changelog и откатом.
* Управление **Яндекс Музыкой** (OAuth-вход, Моя волна, Любимые, поиск, плеер, лайк/дизлайк).
* Таймеры и будильники, режим ИИ (`local`/`remote`/`hybrid`), голос и эмоции Qwen3-TTS, режим LED-матрицы и RGB-кольца, Bluetooth-колонка/сопряжение/список устройств, NTP-синхронизация и часовой пояс.

---

## Поддерживаемые устройства

| Board ID | Устройство | Семейство / SoC | Конфиг сборки | Статус |
|---|---|---|---|---|
| `station-max` (`yndxsttion53`) | **AtriStation Max** (Яндекс Станция Макс) | `amlogic-meson64` / Amlogic S905X3 (`sm1`) | `config/boards/station-max.conf` | Полная поддержка |
| `template` | Шаблон для новой колонки | Настраивается (`amlogic-meson64` и др.) | `config/boards/template.conf` | Шаблон портирования |

### Референсная платформа `station-max`
* **Ядро и драйверы (`drivers/atrios/`):** 13 mainline-драйверов (`gowin_led_device`, `quasar_led_screen`, `quasar_led_ring`, `quasar_pcba_ids`, `rotary-volume`, `gpio_keypad`, `zigbee-control`, `ltr308als01`, `leds-is31fl32xx`, `snd-soc-sy6045s`, `snd-soc-es8156`, `snd-soc-es7210`, `snd-soc-tas5825m`).
* **Фреймбуферы:** `/dev/fb_screen` (LED-матрица 25×16), `/dev/fb_ring` (24 RGB-светодиода), HDMI (`mesondrmfb`).
* **Беспроводные интерфейсы:** Realtek RTL8822CS (Wi-Fi SDIO `rtw88_8822cs`, Bluetooth 5.0 Serdev `/dev/ttyAML1`), Zigbee Silicon Labs EFR32 (`/dev/ttyAML2`).
* **Аудио (`AML-AUGESOUND`):** усилители SY6045S (`0x2a` твитеры, `0x2b` вуфер), ЦАП ES8156 (`0x08`), АЦП ES7210 (`0x40`), 8-канальный PDM-массив микрофонов.

---

## Голосовые команды (слово активации: «Атри» / «Atri»)

| Категория | Примеры фраз | Выполнение |
|---|---|---|
| **Яндекс Музыка** | «Включи мою волну», «Включи Яндекс Музыку», «Включи любимые треки», «Поставь <трек>», «Лайк» / «Нравится», «Дизлайк», «Что сейчас играет?» | Локально + Yandex Music API |
| **Управление музыкой** | «Пауза», «Продолжи», «Следующий трек», «Предыдущий трек», «Выключи музыку» | Yandex Music или BT AVRCP |
| **Громкость** | «Громче», «Тише», «Громкость 50 процентов», «Выключи звук» | Локально (мгновенно) |
| **Таймеры и будильники** | «Поставь таймер на 5 минут», «Сколько осталось?», «Отмени таймер», «Поставь будильник на 7:30», «Удали будильник» | Локально (оффлайн) |
| **Время** | «Сколько времени?», «Который час?» | Локально (оффлайн) |
| **Bluetooth** | «Включи блютуз колонку», «Выключи блютуз», «Режим сопряжения» | Локально (оффлайн) |
| **Подсветка и экран** | «Включи подсветку радуга», «Выключи подсветку», «Покажи часы» | Локально (оффлайн) |
| **ИИ-диалог** | Любой свободный вопрос или запрос (поддерживается прерывание словом «Стоп» / «Хватит») | GPU AtriAI + Qwen3-TTS |

---

## Единая утилита `atrios`

```bash
# Система и диагностика
atrios status                                      # Обзор Wi-Fi, Bluetooth и Zigbee
atrios info                                        # Системная сводка (fastfetch)
atrios check                                       # Аудит аудиотракта и I2C-шин
/usr/lib/atrios/hardware                           # Вывод /etc/atrios/hardware.json

# Экран (25x16), LED-кольцо (24 RGB) и датчик освещённости
atrios screen clock|kisser|eyes|ticker "TEXT"|off  # Режимы LED-матрицы
atrios ring rainbow|amber|volume N|off             # Режимы LED-кольца
atrios sensor                                      # Показания датчика освещённости (lux)

# Аудио, беспроводные интерфейсы и время
atrios volume [0..100%]                            # Громкость (с лимитером баса >40%)
atrios mute [on|off]                               # Отключение звука
atrios wifi [scan|connect SSID PASS]               # Управление Wi-Fi
atrios bt [init|speaker on|off|pair|hide|paired]   # Управление Bluetooth-колонкой
atrios zigbee [reset|ping]                         # Диагностика EFR32 Zigbee
atrios time [status|sync|tz Europe/Moscow]         # NTP-синхронизация и часовой пояс

# Голосовой ассистент, OTA и сброс
atrios ai server https://voice.example.org         # Настройка HTTPS-сервера AtriAI
atrios ai enable                                   # Включить голосового ассистента
atrios ota [status|check|download|update|rollback] # Подписанные OTA-обновления
atrios reset                                       # Сброс до заводских настроек
```

---

## Сборка, тестирование и прошивка

### Основная сборка Mainline (`Makefile` / `compile.sh`)

```bash
# Список поддерживаемых плат и информация о профиле
./compile.sh list
./compile.sh info BOARD=station-max

# Полная сборка загрузочного образа eMMC (kernel + U-Boot .deb -> micro Debian rootfs -> image)
make build
# или явно с выбором платы и имени релиза:
./compile.sh BOARD=station-max RELEASE=Nebula TARGET=image

# Отдельные цели
make kernel       # Компиляция Linux 7.x + DTB + драйверы drivers/atrios/
make uboot        # Компиляция Mainline U-Boot + подпись FIP
make packages     # Сборка .deb пакетов ядра и загрузчика
make rootfs       # Сборка Super Micro Debian arm64 rootfs (zero-Python)
```

### Тестирование и кросс-компиляция рантайма

```bash
# Юнит-тесты Rust-рантайма (на хосте)
rustc --edition=2021 --test board/yandex/station2/src/atrios_runtime.rs -o /tmp/atrios_test && /tmp/atrios_test

# Кросс-сборка единого бинарника atrios-runtime (aarch64)
rustc --edition=2021 --target aarch64-unknown-linux-gnu -O -C strip=symbols   -C linker=aarch64-linux-gnu-gcc board/yandex/station2/src/atrios_runtime.rs   -o board/yandex/station2/rootfs_overlay/usr/lib/atrios/atrios-runtime

# Кросс-сборка bt-speaker-daemon (aarch64)
aarch64-linux-gnu-gcc -O2   -Ibuildroot/output/build/bluez5_utils-headers-5.86/lib   -Ibuildroot/output/build/sbc-2.1   board/yandex/station2/src/bt_speaker_daemon.c   -o board/yandex/station2/rootfs_overlay/usr/sbin/bt-speaker-daemon -ldl -lm

# Автоматическая проверка сборочных и рантайм-регрессий
make test
make verify
```

### Структура репозитория

```text
config/                  # Профили плат (boards/), семейств (families/), загрузчиков (bootloaders/)
drivers/atrios/          # 13 mainline-драйверов ядра Linux 7.x
board/yandex/station2/
  mainline/              # DTS, defconfig ядра и U-Boot, патчи Linux 7.x, aml_burn
  src/                   # atrios_runtime.rs (+ модули), atri_aec.c, bt_speaker_daemon.c
  rootfs_overlay/        # Конфиги, init-скрипты, прошивки, atrios-runtime + симлинки (23 МБ)
lib/                     # Модули системы сборки (kernel, packages, rootfs-debian, image-emmc)
scripts/                 # build_mainline_uboot.sh, verify_all.sh
tools/                   # create_ota_bundle.py, stage_ota_runtime.py, run_voice_https.py
tests/                   # Регрессионные и многоуровневые тесты (Tiers 1-4)
```
