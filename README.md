# AtriOS Nebula

**AtriOS Nebula** — модульный фреймворк и дистрибутив на базе **Mainline Linux (`AtriOS-aml`)** и **Mainline U-Boot** для умных колонок и встраиваемых аудиоплатформ от **leftymods**.

Фреймворк объединяет профили сборки разных устройств (`config/boards/*.conf`, `config/families/*.conf`), набор перенесённых на mainline-ядро драйверов (`drivers/atrios/`), единую CLI-утилиту `atrios`, автоопределение железа в рантайме (`/usr/lib/atrios/device-detect`), OTA-обновления и полный сброс до заводского состояния.

* **Поддерживаемые устройства:** [DEVICES.md](DEVICES.md)
* **Поддерживаемые драйверы:** [DRIVERS.md](DRIVERS.md)

---

## Общая архитектура фреймворка

### 1. Конфигурация и сборка (`config/`, `scripts/`)
* **Профили семейств и плат:** `config/families/*.conf` и `config/boards/*.conf` задают архитектуру, SoC, загрузчик, DTS и rootfs overlay для каждого устройства.
* **Mainline U-Boot:** скрипт сборки и FIP-подписи `scripts/build_mainline_uboot.sh` с поддержкой USB OTG и `fastboot`.
* **Mainline Linux (`AtriOS-aml`):** единое дерево ядра Linux 7.x с внешним набором драйверов `drivers/atrios/` (подробный статус по платам — в [DRIVERS.md](DRIVERS.md)).

### 2. Userspace и сервисы (`rootfs_overlay`)
* **Init-система:** `S00resizefs` (авторасширение rootfs с учётом профиля платы), `S01modules`, `S02baseline` (снимок базовых пакетов и шаблонов), `S03amplifiers`, `S40network`, `S50pipewire`, `S60atrios`, `S71atrios-bt-control`.
* **Аудио и беспроводной стек:** PipeWire + WirePlumber, BlueZ 5 + BlueALSA, `bt-speaker-daemon`, `wpa_supplicant`, ALSA.
* **Автоопределение и диагностика:** `/usr/lib/atrios/device-detect`, `/usr/bin/fastfetch` (динамически считывает SoC, фреймбуферы и звуковые карты).

---

## Референсная конфигурация: `station-max` (Amlogic S905X3 / SM1)

Для платы `station-max` (`board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts`):
* **Фреймбуферы:** `/dev/fb0` (HDMI `mesondrmfb`), `/dev/fb1` (`/dev/fb_screen`, матрица 25×16), `/dev/fb2` (`/dev/fb_ring`, 24 RGB-светодиода).
* **Беспроводные интерфейсы:** Realtek RTL8822CS (Wi-Fi SDIO `rtw88_8822cs`, Bluetooth 5.0 Serdev `/dev/ttyAML1`), Zigbee Silicon Labs EFR32 (`/dev/ttyAML2`).
* **Аудио (`AML-AUGESOUND`):** НЧ/ВЧ усилители SY6045S (`2-002b`, `2-002a`) с DSP-калибровкой, ЦАП ES8156 (`2-0008`), АЦП ES7210 (`2-0040`) и 8-канальный PDM-массив микрофонов.

---

## Единая утилита `atrios`

Все операции управления системой, периферией, обновлениями и сбросом собраны в `/usr/bin/atrios`:

```bash
# Общий статус и система
atrios status                           # Обзор Wi-Fi, Bluetooth и Zigbee
atrios info                             # Системная сводка (fastfetch)
atrios check                            # Аудит аудиотракта и шин

# Экран, LED-кольцо и датчики
atrios screen clock|kisser|eyes|off     # Часы, анимации или отключение матрицы
atrios ring rainbow|volume N|off        # Подсветка верхнего кольца
atrios sensor                           # Показания датчика освещённости (lux)

# Аудио и беспроводные интерфейсы
atrios volume [0..100%]                 # Управление громкостью
atrios mute [on|off]                    # Отключение звука
atrios wifi [scan|connect SSID PASS]    # Управление Wi-Fi
atrios bt [init|scan]                   # Управление Bluetooth
atrios zigbee [reset|ping]              # Диагностика EFR32 Zigbee

# OTA-обновления и сброс к заводским настройкам
atrios ota status                                  # Версия и состояние обновления
atrios ota check                                   # Последний стабильный GitHub Release
atrios ota download                                # Скачать пакет для своей платы
atrios ota update [--reboot]                        # Скачать, проверить подпись и установить
atrios ota install <pkg.tar.gz> [--reboot]           # Установить локальный подписанный пакет
atrios ota rollback [--reboot]                      # Восстановить предыдущие файлы
atrios reset                                        # Полный сброс устройства (двойное подтверждение)
```

---

## OTA-обновления и Factory Reset

### HTTPS-клиент обновлений (`atrios ota`)

Станция сама обращается к GitHub Releases по HTTPS и каждые шесть часов скачивает
новый стабильный пакет своей платы. Входящий порт для обновлений не открывается.
Настройки находятся в `/etc/atrios/updates.json`; по умолчанию используется
`leftymods/AtriOS-linux`. Фоновая служба `S85atrios-update` только скачивает пакет;
команда `atrios ota update` выполняет установку. Более старые версии отклоняются.
APK-релизы `app-v*` учитываются отдельно: при таком latest клиент ищет числовой
релиз прошивки в списке релизов. Выпуск прошивки не меняет latest приложения.

Release asset называется `AtriOS-<board>-ota.tar.gz`, где `<board>` точно совпадает
с `/etc/atrios-board` (`station-max` или `yndxsttion53`). Проверяются размер и
SHA-256 скачанного файла. При установке обязательны RSA/SHA-256 подпись
`manifest.sig`, доверенный ключ `/etc/atrios/ota-public.pem`, совпадение платы,
версии GitHub-релиза и подписанного manifest, а также хеш каждого payload.
Без доверенного ключа установка завершается ошибкой; проверка TLS не отключается.

Пакет может содержать ядро, DTB и rootfs overlay из обычных файлов, включая
модули соответствующего `kernelrelease`. Произвольные `.deb` maintainer scripts,
ссылки, устройства и выходящие за каталог пути не исполняются и не извлекаются.
До записи проверяется весь пакет и свободное место. Изменения записываются через
временные файлы, резервные копии и устойчивый журнал; `S04atrios-ota-recover`
восстанавливает прерванную файловую транзакцию при следующей загрузке Linux.
Это не A/B-обновление: если новое ядро вообще не загружается, автоматический
откат загрузчиком отсутствует, потребуется recovery. Аппаратная устойчивость
FAT/eMMC при отключении питания требует отдельной проверки.

Workflow `.github/workflows/release-mainline.yml` создаёт подписанные пакеты и
полные образы для GitHub Releases. Для выпуска требуется secret `OTA_SIGNING_KEY`
с PEM приватным RSA-ключом. Производный публичный ключ включается в образ;
приватный ключ хранится только во временном каталоге runner. Уже установленные
станции должны получить этот публичный ключ доверенным способом перед первым OTA.
Релизы используют числовые версии, например `v1.1.0`.

### Голосовой клиент HTTPS

WebSocket-клиент удалён. Станция отправляет WAV в `POST /asr`, затем JSON в
`POST /chat` и `POST /tts`; сервер возвращает JSON распознавания/ответа и WAV.
Эти HTTP endpoints уже есть в backend AtriAI. На сервере нужен HTTPS с валидным
сертификатом, например TLS reverse proxy перед backend на `127.0.0.1:8090`.
Вместо reverse proxy можно запустить `tools/run_voice_https.py` через окружение
backend с `--backend-dir`, `--cert`, `--key`, `--token-file`, `--host` и `--port`.
Он подключает только четыре HTTPS-маршрута, требует token вне loopback и не
скачивает веса моделей. LLM-сервисы, указанные в конфигурации backend, запускаются
отдельно; `--warmup` включает прогрев ASR/TTS из локальных весов.

```bash
atrios ai server https://voice.example.org
atrios ai enable
/etc/init.d/S80atriai restart
```

В `/var/lib/atrios/ai.json` можно указать `ca_file` для своей CA и `token` для
Bearer-аутентификации reverse proxy. Адрес задаётся без `/voice`; прежние
HTTP/WS-адреса конфигурации переводятся на HTTPS при чтении. Проверка hostname
и сертификата обязательна. Станция выступает клиентом и не слушает сетевой порт.

### Индикация ошибок

Матрица показывает код, кольцо пульсирует янтарным. Выключенный микрофон сохраняет
приоритет сплошного красного кольца. Анимации не перезаписывают индикацию ошибки.
Коды и текст также доступны в Bluetooth-телеметрии (`errors`).

| Код | Причина |
| --- | --- |
| E01 | Сеть или таймаут |
| E02 | TLS/сертификат |
| E03 | Голосовой сервер или неверный ответ |
| E04 | Воспроизведение аудио |
| E05 | Обновление |
| E06 | Подпись, целостность или отклонённая версия |
| E07 | Носитель или нехватка места |

Успешная операция снимает ошибку своего подсистемного источника; временные
ошибки истекают через две минуты. Bluetooth-команды управления принимаются
по защищённому RFCOMM от известных сопряжённых устройств либо в локально
открытом окне `atrios bt pair`; число клиентов и размер команды ограничены.

### Factory Reset (`atrios reset`)
* Требует двойного интерактивного подтверждения (`YES` → `RESET`; для скриптов поддерживается `--yes`).
* Сравнивает установленные `dpkg`-пакеты с эталоном `/etc/atrios-base-packages.list` и удаляет все доустановленные пользователем пакеты (`apt-get purge` + `autoremove`), `pip`-пакеты, аккаунты с `UID >= 1000` и `/home/*`, учётные данные Wi-Fi, сопряжения Bluetooth, SSH/Dropbear-ключи, истории и состояние `/var/lib/atrios/*`.

---

## Сборка, верификация и прошивка

```bash
# Проверка компиляции DTS, драйверов и userspace-скриптов
bash ./scripts/verify_all.sh

# Сборка ядра и образа
bash ./scripts/build_kernel_and_image.sh

# Сборка Mainline U-Boot
bash ./scripts/build_mainline_uboot.sh
```

### Прошивка (UART + Fastboot)
Прервите загрузку в U-Boot (`fastboot 0`), затем прошейте разделы:
```bash
fastboot flash bootloader u-boot.bin
fastboot flash boot boot.img
fastboot flash rootfs rootfs.ext4
fastboot reboot
```

---

## Авторство и лицензия
* **Разработчик:** leftymods `<ggalab33@gmail.com>`
* **Лицензия:** GPL-2.0-or-later / MIT
