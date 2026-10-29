# Отчёт о сборке и проверке ядра / артефактов (mainline)

Статус: **сборка проходит, обязательные проверки PASS; образ `v130` устарел
относительно текущего дерева после того, как в дерево лёг Wi-Fi патч 0010**
(подробности в §7 и §8).
Дата проверки: 2026-10-29, ~07:44–08:20 MSK.
Плата: Yandex Station Max (Amlogic SM1 / S905X3).
Ветка: `mainline`. Коммитов не делал.

```
дерево ядра:  /home/leftymods/mainline-src/linux-7.3   (Linux 7.3.0, распакованный tarball)
репозиторий:  /home/leftymods/atrios-mainline          (ветка mainline)
toolchain:    aarch64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
jobs:         6
release:      Linux 7.3.0-atrios
```

Важно: дерево и `` во время проверки правили другие агенты
(Wi-Fi порт 0010, `overlay/`, `firmware/`, U-Boot burn). В §7 зафиксировано,
что и когда изменилось, чтобы цифры ниже можно было однозначно сопоставить
с состоянием репозитория.

---

## 1. Вердикт

| Проверка | Результат |
|---|---|
| `apply.sh --kernel-only` идемпотентен | **FAIL** на первом прогоне (0009), **PASS** после обхода и на всех повторных прогонах — §8.1 |
| `build-kernel.sh --strict` | **PASS** (`rc=0`), компиляторных warning/error нет |
| `Image.gz`, board DTB, `modules.tar` собраны | **PASS** |
| Обязательные символы `.config` (12 шт.) | **PASS** (все `y`/`m`, см. §4) |
| `.ko` кодеков `es7210` / `es8156` / `sy6045s` | **PASS** (в `modules.tar`, vermagic `7.3.0-atrios`) |
| Вкомпилированные драйверы в `System.map` | **PASS** (`ringfb`, `ledpanel`, `atrios_keypad`, `rotary_volume`) |
| DTB: 72 канала ring, keypad, rotary, panel, BT, audio card + 4 кодека, i2c2 | **PASS** (§6) |
| `ampack verify` образа `v130` | **PASS** (`rc=0`, «Verified image») |
| `boot.PARTITION`: DTB == наш DTB | **PASS** |
| `boot.PARTITION`: kernel == текущий `Image.gz` | **FAIL (устарел)**: образ содержит ядро сборки 07:48, текущее ядро отличается метаданными (строка IKCONFIG нового символа 0010 + timestamp UTS_VERSION) — §7 |

Коротко: ядро и DTB собираются и содержат всё, что требовалось. Единственный
функциональный FAIL — рассинхронизация test-образа с ядром, собранным уже
после Wi-Fi патча; образ нужно пересобрать после того, как Wi-Fi владелец
добавит `CONFIG_RTW88_8822ES=m` в `configs/atrios.config` (§8.5).

---

## 2. Команды, коды возврата, ключевые строки

### 2.1 Интеграция патчей

```sh
cd /home/leftymods/atrios-mainline
./scripts/apply.sh --kernel-only
```

Первый прогон (07:44), **rc=1**:

```
[+] already applied: patches/linux/0008-soc-es8156.patch
[x] patch does not apply to /home/leftymods/mainline-src/linux-7.3: patches/linux/0009-soc-sy6045s.patch
    Either the patch is stale against this source version, or it was updated
    after this tree applied it (see .../.atrios-mainline-patches).
    Nothing was changed. ...
```

Обход документированным способом (patch 0009 пропускается, остальные шаги
выполняются), **rc=0**:

```sh
PATCH_SKIP='0009-*' ./scripts/apply.sh --kernel-only
# [+] skipping patches/linux/0009-soc-sy6045s.patch (PATCH_SKIP=0009-*)
# [+] installed drivers/audio/sy6045s.c -> .../sound/soc/codecs/sy6045s.c
```

Дальнейшие прогоны без обхода — **rc=0**, все патчи (0002–0010) `already applied`:

```sh
./scripts/apply.sh --kernel-only        # rc=0
# [+] already applied: patches/linux/0009-soc-sy6045s.patch
# [+] already applied: patches/linux/0010-rtw88-8822e.patch
# [+] done
```

### 2.2 Сборка ядра

Команда из задания в текущем виде **не работает** (`build-kernel.sh` принимает
`-j N` / `--jobs N`, но не слитный `-j6`):

```sh
./scripts/build-kernel.sh -j6 --strict
# [x] unknown option: -j6 (try --help)
# rc=1
```

Эквивалентная рабочая форма (использована для проверки), **rc=0**:

```sh
./scripts/build-kernel.sh --jobs 6 --strict
```

Итог второй (актуальной) сборки — 08:14–08:16, `EXIT=0`:

```
[+] kernel:  /home/leftymods/mainline-src/linux-7.3
[+] build:   .../out/linux/build
[+] cross:   aarch64-linux-gnu-gcc (15)
[+] jobs:    6
...
[+] building Image.gz + dtbs + modules (-j6)
  SYNC    include/config/auto.conf
  UPD     kernel/config_data
  GZIP    kernel/config_data.gz
  CC      kernel/configs.o
  ...
  GZIP    arch/arm64/boot/Image.gz
[+] staged 14 dtb(s)
[+] installing modules (INSTALL_MOD_STRIP=1)
...
[+] artifacts in .../out/linux
      15699408  .../Image.gz
      22049847  .../modules.tar
         57965  .../meson-sm1-yandex-station-max.dtb
```

`grep -E 'warning:|error:'` по логу — пусто (компилятор чистый). Скрипт
`--strict` при этом печатает 20 строк `[!] value differs` и **не падает** —
см. §8.3.

### 2.3 Прочие проверки

```sh
for f in scripts/*.sh; do bash -n "$f"; done   # все OK
shellcheck                                               # не установлен (см. §8.7)
gzip -t out/linux/Image.gz                               # OK
dtc -I dtb -O dts out/linux/meson-sm1-yandex-station-max.dtb  # rc=0
```

`make O=... Image.gz dtbs modules -q` для контроля «ничего не пересобирать»
непригоден: kbuild пересоздаёт включённые `auto.conf`/`.config` даже под `-q`
(наблюдал churn), поэтому актуальность проверялась обычным `make` — повторный
запуск после сборки компилирует только изменившееся.

### 2.4 Образ

```sh
./tools/bin/ampack verify out/images/atrios-mainline-test-v130.img
# rc=0, последняя строка: Verified image at '.../atrios-mainline-test-v130.img'
./tools/bin/ampack unpack out/images/atrios-mainline-test-v130.img /tmp/.../unpack
# rc=0
```

---

## 3. Артефакты (актуальная сборка, 08:15–08:16)

| Артефакт | Размер, B | sha256 |
|---|---:|---|
| `out/linux/Image.gz` | 15 699 408 | `323303016332ec9560e1df0e4a35afa24b3adec20dbd8f4fe9611a665ecad408` |
| `out/linux/meson-sm1-yandex-station-max.dtb` | 57 965 | `4960e053d2658e58fe1e8fc1c0e762866ef61055ef3741a47fbb726d477e7778` |
| `out/linux/modules.tar` | 22 049 847 | `d14ba35a3150baea321e790f81cfa8249128e554ade7e9997b10f538a36cbd30` |
| `out/linux/build/arch/arm64/boot/Image` (несжатый) | 44 542 464 | — |

В `modules.tar` — 1 678 файлов `.ko`, `modules.dep{,.bin}`, `modules.alias{,.bin}`.

Модули кодеков (из `modules.tar`, strip; vermagic `7.3.0-atrios SMP preempt
mod_unload aarch64`):

| Модуль | Размер, B | sha256 |
|---|---:|---|
| `snd-soc-es7210.ko` | 22 864 | `885b3d46f8ff92557ff5a7cde1250df87c65fdfd1f638fc9036cd67838441e35` |
| `snd-soc-es8156.ko` | 20 408 | `07b36b69dd87301bc3578e37fe194d196d7f73774e16fc869abead6a211e778e` |
| `snd-soc-sy6045s.ko` | 28 296 | `983557823a2368f7e4327d292e9475aee414cc437e105301ba3ee3416411760e` |

Сборка до патча 0010 (07:48, именно её содержит образ `v130`) дала
`Image.gz` 15 699 393 B / `49043ebe2b817e0562c3948617b74cb57ee3f0661593df6e7b8f513de14ff943`
и `modules.tar` 22 045 856 B / `5d94e6fc0a2a2b869073e5aef0e5aad893719d1112464cd4f8ae7db4911df185`;
DTB не менялся. Разница ядра — метаданные (строка IKCONFIG нового символа и
timestamp сборки в UTS_VERSION, см. §7.1).

---

## 4. Конфигурация ядра

`out/linux/build/.config` после `defconfig` + `configs/atrios.config` +
`olddefconfig`:

| Символ | Значение |
|---|---|
| `CONFIG_LEDS_TRIGGER_RINGFB` | `y` |
| `CONFIG_FB_ATRIOS_LEDPANEL` | `y` |
| `CONFIG_INPUT_ROTARY_VOLUME` | `y` |
| `CONFIG_INPUT_ATRIOS_KEYPAD` | `y` |
| `CONFIG_LTRF216A` | `y` |
| `CONFIG_SND_SOC_ES7210` | `m` |
| `CONFIG_SND_SOC_ES8156` | `m` |
| `CONFIG_SND_SOC_SY6045S` | `m` |
| `CONFIG_BT_HCIUART_RTL` | `y` |
| `CONFIG_MMC_MESON_GX` | `y` |
| `CONFIG_SPI_MESON_SPICC` | `y` |
| `CONFIG_I2C_MESON` | `y` |

Дополнительно проверено: `CONFIG_LOCALVERSION="-atrios"`,
`build/include/config/kernel.release` = `7.3.0-atrios`.

Отклонения от буквы фрагмента (не влияют на обязательный список, но
`--strict` их не ловит — §8.3): `CONFIG_SOUND=m` тянет весь `SND*` в `m`
(фрагмент просит `y`), `CONFIG_DWMAC_MESON=m` вместо `y`
(`CONFIG_STMMAC_PLATFORM=m`).

---

## 5. Вкомпилированные драйверы (`System.map`)

| Символ | Адрес |
|---|---|
| `ringfb_driver_init` | `ffff8000821b129c t` |
| `ledpanel_driver_init` | `ffff8000821b2dd4 t` |
| `atrios_keypad_probe` | `ffff800080daec44 t` |
| `rotary_volume_probe` | `ffff800080db0980 t` |

Плюс `ringfb_ops`, `ledpanel_fb_ops`, `atrios_keypad_of_match`,
`rotary_volume_of_match` и остальные функции — все в `System.map`.
`of_match` таблицы драйверов совпадают с DTS: `yandex,led-ring-fb`,
`yandex,led-screen`/`atri,led-panel`, `yandex,gpio-keypad`,
`yandex,rotary-volume`; `ltrf216a` матчит `liteon,ltr308` (DTS использует
именно его); `is31fl32xx` — `issi,is31fl3236a`.

---

## 6. Проверка DTB

`out/linux/meson-sm1-yandex-station-max.dtb`, 57 965 B, decompile `dtc` rc=0
(5 warning — все во upstream `meson-g12-common.dtsi`/`meson-sm1.dtsi`:
`unit_address_vs_reg`/`simple_bus_reg` для `pinctrl@40`, `pinctrl@14`,
`avoid_unnecessary_addr_size` для `dsi@7000`; к нашим узлам не относятся).

| Требование | Факт в DTB | Итог |
|---|---|---|
| 72 канала ring | 2 контроллера `issi,is31fl3236a` на `i2c@1f000` (i2c0), по 36 child-узлов; 72 уникальных label `station2:{red,green,blue}:ring1..24` | PASS |
| keypad | `/keypad`, `compatible = "yandex,gpio-keypad"`, `key-names`/`key-codes`/`key-gpios`/`scan_period` | PASS |
| rotary | `/rotary-volume`, `compatible = "yandex,rotary-volume"`, `gpios`, `key-codes` | PASS |
| panel | `/soc/bus@ffd00000/spi@15000/panel@0` (spicc1, `status=okay`), `compatible = "yandex,led-screen","atri,led-panel"`, `max-brightness=200`, reset/JTAG gpio | PASS |
| Bluetooth | `/soc/bus@ffd00000/serial@24000` (uart_A, `status=okay`) → `/bluetooth`, `compatible = "realtek,rtl8822cs-bt"` | PASS |
| аудио-карта | `/sound`, `compatible = "amlogic,axg-sound-card"`, 5 `dai-link` | PASS |
| 4 кодека | `i2c2` (`i2c@1d000`, `status=okay`): `audio-codec@8` everest,es8156; `@2a`/`@2b` silergy,sy6045s; `@40` everest,es7210. В `/sound/dai-link-3/codec-0..3` phandle 76/77/78/79 — ровно эти четыре узла | PASS |
| i2c2 | `/soc/bus@ffd00000/i2c@1d000`, `status=okay`, pinctrl `i2c2_sda_z/sck_z` | PASS |
| ringfb | `/led-ring-fb`, `compatible = "yandex,led-ring-fb"` | PASS |

Примечание: `i2c0` в mainline — это `i2c@1f000` (не `1c000`), поэтому ring,
EEPROM и light sensor лежат на `i2c@1f000`; `i2c2` — `i2c@1d000` (шина
кодеков). Всё соответствует комментариям в DTS.

---

## 7. Образ `out/images/atrios-mainline-test-v130.img`

| | |
|---|---|
| Размер | 1 571 973 256 B |
| sha256 | `209bc24411305c91e6873ac2dfae226bf1b254dfae575f8be5c492b113cd4da0` |
| Файл `.sha256` | совпадает (`sha256sum -c` из `out/images` проходит) |
| `ampack verify` | **rc=0**, `Verified image at '...'` |
| Штамп в rootfs | `/etc/atrios-test-image`: `Linux 7.3.0-atrios (mainline)`,
  `built: 2026-10-29T04:19:21Z` |

Состав (12 файлов + 5 `VERIFY/*`, как в `docs/image.md`): `DDR.USB`,
`UBOOT.USB`, `bootloader.PARTITION` (1 453 936 каждый), `aml_sdc_burn.UBOOT`
(1 454 448), `aml_sdc_burn.ini` (602), `platform.conf` (155), `logo.PARTITION`
(1 338 688), `_aml_dtb.PARTITION`/`meson1.dtb`/`meson1_ENC.dtb` (61 440),
`boot.PARTITION` (15 761 408), `system.PARTITION` (1 551 892 480).

### 7.1 `boot.PARTITION`

Разобран после `ampack unpack` (файл 15 761 408 B, page-aligned):

```
magic        ANDROID!
kernel       15 699 393 B  @ 0x20000000
ramdisk      0 B          @ 0x01000000
second(dtb)  57 965 B     @ 0x00f00000
tags         0x100, page_size 2048, kernel_version 1, os_version 0
header_size  1648 (Android boot header v1)
cmdline      307 B: root=/dev/mmcblk0p18 rootfstype=ext4 rw rootwait
             skip_initramfs init=/init selinux=0 enforcing=0 ... console=ttyS0,115200
second_off   0x800;  файл заканчивается ровно на second_off + pad(dtb)
```

Сверка содержимого:

| Что | В образе | Артефакт | Итог |
|---|---|---|---|
| kernel | `49043ebe…ff943` (15 699 393 B) | сборка 07:48: `49043ebe…` | PASS на момент сборки образа |
| kernel | `49043ebe…ff943` | текущий `Image.gz` `32330301…` | **FAIL: образ устарел** |
| dtb | `4960e053…7778` | текущий DTB `4960e053…` | PASS |
| 3 `.ko` в `system.PARTITION` | hashes `885b3d/07b36b/983557` | `modules.tar` (07:48 и 08:15) | PASS (байт в байт) |

Причина расхождения ядра: после сборки образа в дерево лёг патч
`patches/linux/0010-rtw88-8822e.patch` (07:50), который добавляет новый
Kconfig-символ `RTW88_8822ES`, и любая пересборка теперь заново генерирует
`config_data` и `utsversion.h`. `Image.gz` меняется 15 699 393 → 15 699 408 B
(+15 B после gzip), несжатый `Image` сохраняет размер 44 542 464 B, но
отличается в 54 101 позиции. Пересобраны только `kernel/configs.o` и
`init/version-timestamp.o` (лог: `UPD kernel/config_data`,
`UPD include/generated/utsversion.h`), т.е. различия — метаданные:
добавленная строка `# CONFIG_RTW88_8822ES is not set` в IKCONFIG (проверено
`extract-ikconfig`) и timestamp сборки в UTS_VERSION. Исходники драйверов
между этими сборками не менялись; `dtb` и все три модуля кодеков побайтово
те же.

Вывод: механизм упаковки `make-image.sh` проверен и корректен (для своего
снапшота совпадение точное), но `v130` — это снапшот до Wi-Fi патча.
После финализации Wi-Fi порта образ нужно пересобрать (§8.5), иначе в нём
не будет ни нового ядра, ни `rtw88_8822es.ko`.

### 7.2 `system.PARTITION`

ext4 (UUID `57f8f4bc-…`), `debugfs` читает. Внутри `/lib/modules/7.3.0-atrios/…`
присутствуют все три кодека (даты 28-Oct 19:55, hashes совпадают с §3),
`/lib/firmware/sy6045s-{tweeters,woofer}-settings.txt` на месте.
`rtw88_8822es.ko` в образе **отсутствует** (патч новее образа).

---

## 8. Найденные проблемы

### 8.1 `apply.sh --kernel-only` не идемпотентен для патча 0009 (HIGH, владелец: scripts)

Симптом (rc=1, первый прогон):

```
[x] patch does not apply ... patches/linux/0009-soc-sy6045s.patch
```

Диагноз:
- в `.atrios-mainline-patches` записан старый sha256 патча
  `87fde140…` (0009 применялся 28-Oct), на диске патч уже новый
  `1c910112…` (обновлён 28-Oct 19:54);
- `patch_applied()` делает `git apply --reverse --check`; для hunk'а
  «создать `sound/soc/codecs/sy6045s.c`» reverse-check падает, потому что
  `apply.sh` сам перезаписал этот файл из `drivers/audio/sy6045s.c`
  (версия без правки комментария). Форвардная проверка видит «файл уже есть»
  и тоже отказывает;
- при этом Kconfig/Makefile-хунки уже применены, т.е. дерево консистентно,
  не хватало только свежего текста `sy6045s.c` (разница — 2 строки
  комментария).

Обход (`PATCH_SKIP='0009-*'` + повторный прогон) привёл дерево в целевое
состояние; сейчас все патчи 0002–0010 проходят как `already applied`
(rc=0). Но «чистый» повторный `apply.sh` на таком дереве падает — это
нарушает заявленную идемпотентность.

Предложение владельцу scripts: если в маркере есть тот же патч с другим
хэшем, а форвард говорит «already exists» только для файлов, которые
`drivers/**` всё равно копирует, — не `die`, а перезаписать копией
и обновить хэш в маркере. Либо задокументировать обязательный
`PATCH_SKIP`/reset при обновлении 0009.

### 8.2 `build-kernel.sh` не принимает `-j6` (LOW, scripts)

`[x] unknown option: -j6 (try --help)`, rc=1. Поддерживается только
`-j 6` / `--jobs 6`; README использует `-j "$(nproc)"`, поэтому это
несоответствие только в формулировке задачи/привычной записи. Либо добавить
поддержку слитной формы (`-j*`), либо не использовать её в инструкциях.

### 8.3 `--strict` не ловит расхождение значений; ложные срабатывания (MED, scripts)

`check_fragment()`:
- сравнивает строку патча целиком, вместе с хвостовым `# комментарием`,
  поэтому 6 из 20 предупреждений — ложные (`CONFIG_X=y  # ...` vs
  `CONFIG_X=y`): `COMMON_CLK_G12A`, `PINCTRL_MESON_G12A`,
  `SERIAL_MESON_CONSOLE`, `LEDS_IS31FL32XX`, `KEYBOARD_GPIO`,
  `BT_HCIUART_3WIRE`;
- при `value differs` только печатает warning и **не** выставляет
  `missing=1`, поэтому `--strict` завершился rc=0 при 14 реальных
  расхождениях: `DWMAC_MESON` (y→m) и весь блок `SND*` (y→m, потому что
  `CONFIG_SOUND=m`).

Если `--strict` должен ловить «фрагмент применён не полностью» — сравнение
надо делать по `CONFIG_X=value` без комментария, а `value differs` считать
ошибкой.

### 8.4 Аудио-стек собирается модулями, а не в ядро (MED, владелец: configs)

`arm64 defconfig` даёт `CONFIG_SOUND=m`; фрагмент просит `CONFIG_SND=y`,
`SND_SOC=y`, `SND_MESON_*=y`, `DWMAC_MESON=y`, но после `olddefconfig`
всё это `m`. Для test-образа это работает (модули в `modules.tar`), но:
- `/etc/init.d/S01modules` в образе всё ещё `insmod`-ит модули 4.9.113
  (см. `docs/image.md`), автозагрузки 7.3-кодеков нет;
- если нужен встроенный звук, во фрагмент надо добавить `CONFIG_SOUND=y`
  (и `CONFIG_STMMAC_PLATFORM=y` для `DWMAC_MESON=y`).

### 8.5 Wi-Fi патч 0010 не включён в конфиг, модуль выпадает из сборки (HIGH, владелец: Wi-Fi порт + configs)

- `patches/linux/0010-rtw88-8822e.patch` (07:50) добавляет
  `CONFIG_RTW88_8822ES` и `rtw88_8822es.ko`, но в `configs/atrios.config`
  символа нет (там до сих пор комментарий «must not be enabled here yet»).
- Каноническая сборка (`defconfig` + фрагмент + `olddefconfig`) даёт
  `# CONFIG_RTW88_8822ES is not set` (проверено в отдельном `O=` дереве).
  Следовательно, в текущем `modules.tar` **нет** `rtw88_8822es.ko`
  (`grep -c rtw88_8822es modules.tar` → 0), и в образ он не попадёт.
- В рабочем `out/linux/build/.config` символ был включён вручную
  (`CONFIG_RTW88_8822ES=m`, 07:51), из-за чего `make` в 08:06 собрал
  `rtw88_8822es.ko` (vermagic `7.3.0-atrios`, firmware
  `rtw88/rtw8822e_fw.bin`). Этот `.ko` остался только в build-дереве;
  моя пересборка каноническим скриптом вернула `.config` к фрагменту, и в
  `modules.tar` модуль не попадает (там его не было и до этого, т.к.
  `out/linux` не пересобирался после 0010).

Владельцу Wi-Fi: после ревью добавить `CONFIG_RTW88_8822ES=m` в
`configs/atrios.config` (и убрать противоречащий комментарий), затем
пересобрать ядро и образ.

### 8.6 Дубликат записи 0009 в маркере (INFO)

`.atrios-mainline-patches` содержит две строки для 0009 (старый и новый
sha256) — следствие обновления патча; на работу не влияет, но маркер стоит
почистить при следующем reset дерева.

### 8.7 Линтеры

- `bash -n` для всех `scripts/*.sh` — PASS.
- `shellcheck` в окружении не установлен, прогнать не смог (CI линтит
  скрипты отдельно). Это единственная проверка, которую я не выполнил.

### 8.8 Прочее (вне моей зоны, к сведению)

- `dtc` warning'и по upstream `pinctrl@40`/`pinctrl@14`/`dsi@7000` —
  не наши узлы.
- `make -q` в kbuild пересоздаёт `.config`/`auto.conf`; для проверки
  «up-to-date» использовать обычный `make` и смотреть, что ничего не
  компилируется.
- Сборка ядра **не бит-репродуцируема**: `KBUILD_BUILD_TIMESTAMP` попадает
  в `include/generated/utsversion.h` (`UPD … utsversion.h` при каждой
  пересборке после изменения конфига), поэтому `Image.gz` меняется даже без
  правок кода. Сверка `boot.PARTITION.kernel == out/linux/Image.gz` имеет
  смысл только в пределах одной сборки; для релиза фиксируйте sha256
  `Image.gz` (как сейчас делает `docs/image.md`).

---

## 9. Что перепроверить после исправлений

1. Добавить `CONFIG_RTW88_8822ES=m` в `configs/atrios.config`; пересобрать
   `build-kernel.sh --jobs 6 --strict`; убедиться, что
   `tar -tzf modules.tar | grep rtw88_8822es` даёт 1 файл.
2. Починить идемпотентность `apply.sh` для 0009 (или задокументировать
   reset) — повторный прогон на текущем дереве должен быть rc=0 без
   `PATCH_SKIP`.
3. Починить `check_fragment()` (комментарии + `value differs` → ошибка при
   `--strict`) и, при желании, `-j6`.
4. Пересобрать образ (`scripts/make-image.sh`) после финального ядра,
   обновить `docs/image.md` (размер/sha256) и снова прогнать
   `ampack verify` + сверку `boot.PARTITION`.
