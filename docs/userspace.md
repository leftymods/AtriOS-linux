# Userspace overlay для mainline-образа

Этот каталог (`overlay/`) — минимальный текстовый оверлей,
который накладывается на Buildroot-rootfs от 4.9.113 при сборке
mainline-образа.  Он приводит userspace к раскладке PCM/контролов
ядра 7.3 и не содержит бинарников, прошивок и модулей.

Границы: здесь только `/etc` и `/usr/lib/atrios/audio`.  DTS, драйверы,
`patches/`, `scripts/` и U-Boot не затрагиваются.

## Состав

| Файл | Что делает |
|---|---|
| `etc/asound.conf` | `default`: playback `plughw:AMLAUGESOUND,0`, capture `plughw:AMLAUGESOUND,1`; именованные PCM `capture_plug` (ES7210, 2 ch) и `pdm` (PDM, 4 ch) |
| `etc/init.d/S01modules` | грузит только модули 7.3 (`snd-soc-es7210`, `snd-soc-es8156`, `snd-soc-sy6045s`, опционально `rtw88_8822es` или `rtw88_8822cs`); никогда не трогает `/lib/modules/4.9.113` |
| `etc/init.d/S03amplifiers` | заглушка: старый скрипт из rootfs 4.9 делал `/dev/mem`-поке `MST_PAD_CTRL*`; на mainline эти регистры принадлежат clock-драйверу |
| `etc/init.d/S50pipewire` | необязательный PipeWire/WirePlumber; из 4.9-версии убран отложенный вызов `S03amplifiers` |
| `etc/init.d/S60atrios` | запускает `rotaryd` и `screend`, создаёт `/var/log` |
| `usr/lib/atrios/audio` | адаптированный `atrios audio`: карта устройств `1 = ES7210`, `2 = PDM`, без `S03`/`/dev/mem` |

`/usr/lib/atrios/{input,rotaryd,light,screend,check_audio.sh}` **не
копируются**: они уже есть в rootfs 4.9 и совместимы с mainline
(проверено по документации и коду):

* `rotaryd` ждёт `KEY_PAGEUP` (104) / `KEY_PAGEDOWN` (109), а mainline
  `rotary-volume.c` по умолчанию отдаёт именно их; кольцо остаётся
  `/dev/fb5` (24 × RGB888, 72 байта), экран — `/dev/fb4`.
* `input` читает `in_intensity_input` у IIO; upstream-драйвер `ltrf216a`
  для `liteon,ltr308` отдаёт канал `IIO_INTENSITY` с тем же именем.
* `check_audio.sh` уже рассчитан на `AMLAUGESOUND`, I2C-шину 2,
  регулятор `20V_AMPL`, контролы `Woofer/Tweeters/Headphone`, `Loopback
  ADC*_MUTE` и пропускает отсутствующий `SPK mute`.
* `light`/`screend` работают с теми же framebuffer-интерфейсами.

`/etc/modules-load.d` **не используется**: Buildroot здесь на BusyBox
init и не читает systemd-каталоги.  Эквивалент — `S01modules`.

## Отличия от 4.9

### PCM и `/etc/asound.conf`

Вендорская карта: dev 0 = TDM-B (playback+capture), 1 = PDM, 2 =
SPDIF-B, 3 = TDM-C.  Mainline c DPCM создаёт PCM только для DMA
front-end'ов в порядке dai-link'ов:

| PCM | mainline | 4.9 |
|---|---|---|
| 0 | playback → TDM-B | TDM-B (в обе стороны) |
| 1 | capture ← TDM-B (ES7210, 2 слота) | PDM |
| 2 | capture ← PDM (4 канала) | SPDIF-B |
| 3 | нет | TDM-C |

Поэтому:

* `capture.pcm` переключён с `plughw:AMLAUGESOUND,0` на `...,1`.
  На dev 0 захвата больше нет.
* Добавлены `pcm.capture_plug` (fallback в `audio`, теперь 2 канала) и
  `pcm.pdm` (явный доступ к PDM).
* `ctl.!default` не менялся: short name карты остаётся `AMLAUGESOUND`
  благодаря `model = "AML-AUGESOUND"` в DTS.

### `usr/lib/atrios/audio`

* Карта устройств в комментариях и в обоих циклах захвата:
  было `for dev in 0 3 1`, стало `for dev in 1 2`; dev 0 —
  playback-only, dev 3 не существует.
* `capture_plug` теперь просит 2 канала (у TDM-B два слота; 8/4
  каналов на dev 1 закономерно падают, цикл сам пробует 8→4→2).
* Убраны вызовы `/etc/init.d/S03amplifiers` (в `test`, `play`):
  вместо них повторно применяется сохранённая громкость
  (`apply_volume`).  На mainline усилители и pad'ы настраивает ядро.
* `atrios audio amps` больше не перезапускает вендорский скрипт, а
  переписывает имя файла в `settings_file` драйвера `sy6045s`
  (`/sys/bus/i2c/drivers/sy6045s/2-00*/settings_file`) — это
  документированный путь повторного применения прошивки из
  `docs/codec-sy6045s.md`.
* Имена ALSA-контролов не менялись: `Woofer/Tweeters Master Playback
  Volume/Switch`, `Headphone DAC Playback Volume`, `Headphone Headphone
  Switch`, `Loopback ADC*_MUTE`.  `SPK mute` на mainline отсутствует —
  все обращения к нему и так идут с `|| true`.

### Инициализация

* `S01modules` больше не делает `insmod` по путям 4.9 и не поднимает
  `rotary-volume`, `gowin_led_device`, `quasar_led_*`,
  `leds-is31fl32xx`, `ltr308als01`, VPU-декодеры: на mainline это
  либо built-in, либо не нужно.  Остались три кодека и rtw88.
* `rtw88` грузится только если есть модуль **и** соответствующий
  mainline-прошивке файл: `rtw88_8822es` → `/lib/firmware/rtw88/rtw8822e_fw.bin`
  (это чип Station Max, SDIO `024c:a822`), иначе `rtw88_8822cs` →
  `rtw8822c_fw.bin`.  В rootfs 4.9 лежат только вендорские `rtl8822*`,
  поэтому без добавления firmware скрипт печатает понятное
  `[!] ... firmware ... missing (skipped)`.
* `S03amplifiers` — no-op.  Без неё старый файл из rootfs 4.9
  продолжил бы выполняться на boot и лезть в `/dev/mem`.
* `S50pipewire` сохранён, но больше не запускает `S03amplifiers`.
  Если `pipewire` не установлен, печатает «ALSA-only mode».
* `S60atrios` — те же демоны; добавлен `mkdir -p /var/log`.

## Как оверлей применяется при сборке

В `scripts/make-image.sh`, в `build_rootfs()`, после распаковки
rootfs-тарбола и `modules.tar` и **до** `depmod`/`make_ext4fs`.  Сам
скрипт этого пока не делает (он вне зоны этого оверлея); изменение
должно быть таким:

```sh
OVERLAY_DIR="${OVERLAY_DIR:-$MAINLINE_DIR/overlay}"
[ -d "$OVERLAY_DIR" ] || die "userspace overlay not found: $OVERLAY_DIR"
log "applying mainline userspace overlay: $OVERLAY_DIR"
cp -a "$OVERLAY_DIR"/. "$tree"/
```

Порядок важен:

1. `cp -a` должен идти **после** `tar -xf rootfs`, иначе
   `etc/init.d/S01modules` и `S03amplifiers` останутся вендорскими.
2. **До** `depmod -b` и `make_ext4fs`: depmod сам по себе оверлей не
   затрагивает, но права/содержимое должны попасть в `fs_config` и
   итоговый `system.PARTITION`.
3. `-a` сохраняет режимы: init-скрипты должны быть `0755`,
   `asound.conf` — `0644`.  Владелец — тот, от кого идёт сборка; при
   сборке без root/fakeroot `fs_config` запишет текущего пользователя.
   Запускайте сборку так же, как остальной образ (root или fakeroot).
4. Оверлей не удаляет файлы.  Поэтому `S03amplifiers` именно
   перезаписывается заглушкой, а не «удаляется».

Ничего из `overlay/` не является модулем ядра; `modules.tar` и
`depmod` работают независимо.

## Проверка на железе

```sh
uname -r                                  # 7.3.0-atrios
cat /proc/device-tree/model               # Yandex Station Max

# модули: S01 не должен ссылаться на 4.9
/etc/init.d/S01modules start
lsmod | grep -E 'snd_soc_(es7210|es8156|sy6045s)|rtw88'
dmesg | tail -50

# PCM-раскладка
aplay -l                                  # card 0: PCM 0 playback
arecord -l                                # PCM 1 и PCM 2 capture, PCM 3 нет
cat /proc/asound/cards

# запись: ES7210 2 ch и PDM 4 ch
arecord -D hw:0,1 -c 2 -r 48000 -f S32_LE -d 3 /tmp/es7210.wav
arecord -D pdm     -c 4 -r 48000 -f S32_LE -d 3 /tmp/pdm.wav
# arecord -D hw:0,1 -c 4 ... должен ожидаемо упасть (2 слота TDM-B)

# микшер и громкость
amixer -c AMLAUGESOUND scontrols
amixer -c AMLAUGESOUND cget name='Woofer Master Playback Volume'
atrios volume 40
atrios volume
cat /var/lib/atrios/volume

# сквозные проверки
atrios audio test        # внутри вызывает check_audio.sh и пишет в /tmp
atrios audio record 3 /tmp/mic.wav
atrios audio amps        # перечитывает settings_file у sy6045s
```

Кнопки/энкодер/экраны:

```sh
atrios input             # Ctrl+C; крутить ручку -> KEY_PAGEUP/PAGEDOWN
evtest                   # проверить rotary-volume / gpio-keypad
ls -l /dev/fb4 /dev/fb5  # screend / ring
cat /var/log/atrios-rotaryd.log
cat /var/log/atrios-screend.log
```

PipeWire (если установлен):

```sh
pgrep -a pipewire; pgrep -a wireplumber
wpctl status             # карта AML-AUGESOUND и узлы
```

## Риски и ограничения

1. **PipeWire vs прямой ALSA.** `plughw:AMLAUGESOUND,0` открывает
   устройство эксклюзивно.  Если PipeWire уже держит `hw:0,0`,
   `aplay`/`atrios audio play` могут получить `Device or resource busy`;
   `audio play` временно останавливает `S50pipewire`, но сторонние
   клиенты — нет.  Для отладки: `/etc/init.d/S50pipewire stop`.
2. **Имя карты и контролов.** Все скрипты ищут `AMLAUGESOUND` и точные
   имена контролов.  Если в DTS поменять `model` или
   `sound-name-prefix`, `amixer`/`aplay` будут молча падать из-за
   `|| true`.  Проверять `aplay -l` и `amixer scontrols` после каждого
   изменения DTS.
3. **Только 2 канала на ES7210.** Это ограничение TDM-B
   (`dai-tdm-slot-num = <2>`), а не скрипта.  Запрос 8/4 каналов на
   PCM 1 падает; 8-канальный массив на mainline недоступен без
   изменения DTS (4 слота и маска).
4. **PDM.** Путь SM1 PDM включён впервые; уровень/шум могут отличаться
   от 4.9 (нет вендорских `filter_mode`/HCIC shift).  Это не ошибка
   userspace.
5. **`check_audio.sh` из 4.9.** Он предполагает I2C-шину 2,
   `20V_AMPL`, контролы `Ch1/Ch2 = 159` и наличие тестового WAV.
   На mainline возможны ложные FAIL (например, если регулятор ещё не
   зарегистрирован).  Запускать `atrios audio check -v`, а не считать
   `atrios audio test` истиной в последней инстанции.
6. **Wi-Fi/BT.** Модуль rtw88 без mainline-firmware
   (`/lib/firmware/rtw88/rtw8822e_fw.bin` для `rtw88_8822es` или
   `rtw8822c_fw.bin` для `rtw88_8822cs`) не поднимется; S01 в этом
   случае его пропускает.  Файл `firmware/rtw88/rtw8822e_fw.bin`
   должен попасть в rootfs как `/lib/firmware/rtw88/rtw8822e_fw.bin`
   (это делает сборка образа, вне данного оверлея).  Bluetooth-инициализация
   (`rtk-bt-init`) в этот оверлей не входит и требует отдельной
   проверки на mainline.
7. **Старые модули 4.9 остаются в rootfs.** Они не загружаются, потому
   что `modprobe` использует `uname -r`; но не добавляйте
   `/lib/modules/4.9.113` в `depmod` и не вызывайте `insmod` по этим
   путям.
8. **Порядок применения оверлея.** Если `cp -a` окажется после
   `make_ext4fs` или до распаковки rootfs, в образе останутся
   вендорские `S01modules`/`S03amplifiers`; первый начнёт грузить
   4.9 `.ko` (`Invalid module format`), второй полезет в `/dev/mem`.
9. **Номера framebuffer.** `rotaryd`/`screend` жёстко используют
   `/dev/fb5` и `/dev/fb4`.  Порядок регистрации fb на mainline
   проверен документацией (`docs/ring.md`, `docs/screen.md`), но при
   добавлении других fb-устройств номера могут сместиться.
10. **Права.** Если init-скрипты потеряют `+x` при копировании
    (`cp` без `-a`, распаковка архива), BusyBox rcS их пропустит.
