# AtriOS Mainline — карта документации

> **Статус: ничего не проверено на железе.**
> Все устройства в `README.md` помечены `not tested`; «написано/собрано»
> означает только сборку на хосте.

Срез: 2026-10-29, ветка `mainline`, HEAD `eb22770` + незакоммиченное рабочее
дерево (`u-boot/`, `patches/linux/0010`, `firmware/`, `overlay/`, новые docs).
Проект активно правится несколькими людьми одновременно, поэтому статусы ниже
относятся к указанной дате.

Назначение файла: одна точка входа в документацию и **список расхождений**.
Правило: каждый документ правит его владелец; здесь только замечания, без
чужих правок. Точка входа по сборке - `mainline/README.md`.

## 1. Карта документов

| Файл | О чём | Статус |
|---|---|---|
| `README.md` | входная точка: дерево, статусы, сборка, артефакты, требования к финальному образу | актуально (перепроверено и исправлено 2026-10-29) |
| `docs/INDEX.md` | этот файл | актуально |
| `docs/porting-plan.md` | план порта по блокам, порядок работ | актуально как план; статусы блоков не обновлялись (см. §2.17) |
| `docs/hardware-inventory.md` | инвентарь железа по вендорским DTB/модулям/userspace | актуально, кроме `ya,led_screen` (§2.12) |
| `docs/dts-review.md` | ревью генератора DTS против upstream 7.3 + список конфликтов между docs | актуально, но 3 строки устарели (§2.7-2.9) |
| `docs/ring.md` | RE кольца (IS31FL3236A + `led_ring_fb`) и план порта | актуально; битая ссылка (§2.13) |
| `docs/rotary.md` | RE энкодера + драйвер `rotary-volume` | актуально, кроме §Verification (§2.10) |
| `docs/buttons.md` | RE кнопок, почему не `gpio-keys`, драйвер `atrios-keypad` | актуально, кроме §228-231 (§2.11) |
| `docs/ledpanel-driver.md` | драйвер передней матрицы 25x16 (fbdev, PA/SA, SPI) | актуально: драйвер есть и собран, JTAG/bitstream не портированы |
| `docs/screen.md` | полный RE матрицы (Gowin FPGA, JTAG, reflash, touch) | актуально (RE без железа) |
| `docs/audio.md` | аудио: TDM-B/PDM, три кодека, DPCM-карта, PCM-нумерация | актуально (карта смёржена в DTS, DTB собирается; на железе нет) |
| `docs/audio-dts-fragment.dts` | исходный фрагмент аудио-узлов | устарел by design: источник истины - `dts/gen-ring-nodes.py` (сказано в `audio.md` :15-21) |
| `docs/codec-es7210.md` | драйвер ES7210 | актуально |
| `docs/codec-es8156.md` | драйвер ES8156 | актуально |
| `docs/codec-sy6045s.md` | драйвер SY6045S | актуально (компилируется, на железе нет) |
| `docs/bluetooth.md` | план BT (RTL8822CS/ES, `btrtl` + `hci_h5`) | актуально как план + анализ; firmware blocker открыт |
| `docs/wifi.md` | досье RTL8822ES (RE, стратегия stage (a)/(b)) | актуально |
| `docs/wifi-port.md` | реализация stage (a): патч 0010, прошивка, сборка модуля | актуально (модуль собран, в `modules.tar` не входит) |
| `docs/uboot.md` | U-Boot v2026.10: defconfig, DTS, FIP-риски | устарело в 3 местах (§2.4-2.6) |
| `docs/uboot-burn.md` | протокол Amlogic v2 USB burn (RE вендорского BSP) | актуально; нет ссылки на реализацию (§2.15) |
| `docs/uboot-burn-port.md` | порт burn-протокола в mainline U-Boot (реализовано, собирается) | актуально (на железе не проверялось) |
| `docs/image.md` | промежуточный test-образ (вендорский BL33 + наш kernel) | актуально (sha256 сверен); §7.8 см. §2.14 |
| `docs/userspace.md` | overlay `/etc` + `usr/lib/atrios/audio` для mainline-образа | актуально |
| `docs/evidence/*.json` (7 файлов) | REA-артефакты вендорских бинарников | актуально; `dtb-summary.json` отсутствует (§2.13) |

## 2. Расхождения, которые надо исправить владельцам

Проверки выполнялись в `/home/leftymods/atrios-mainline` (файлы) и
`/home/leftymods/mainline-src` (деревья, артефакты).

### 2.1 `configs/atrios.config:88-92` - устаревший комментарий про ALS

Написано: «the board DTS says `"lite-on,ltr308als01"` - the node will not probe
until the DTS compatible (or the driver table) is fixed. Reported, not worked
around».

Факт: DTS уже использует upstream id - `dts/meson-sm1-yandex-station-max.dts:212`
`compatible = "liteon,ltr308"`, то же в собранном DTB
(`fdtget ... /soc/bus@ffd00000/i2c@1f000/light-sensor@53 compatible` →
`liteon,ltr308`). `dts-review.md:64,136` это подтверждает.

Заменить на: «DTS uses the upstream `liteon,ltr308` id (`ltrf216a.c` of_match),
no driver patch needed».

### 2.2 `configs/atrios.config:103-106` - устаревший комментарий про `gpio-keys`

Написано: «The board DTS still carries the upstream `gpio-keys` node ... so both
are enabled until the DTS switches».

Факт: узла `gpio-keys` в DTS нет (`grep -n "gpio-keys" dts/*.dts` → пусто;
в DTB корень содержит только `keypad` с `compatible = "yandex,gpio-keypad"`).
`CONFIG_KEYBOARD_GPIO=y` ещё включён, но привязывать его не к чему.

Заменить на: «`CONFIG_INPUT_ATRIOS_KEYPAD=y` is the only key model in the DTS;
`CONFIG_KEYBOARD_GPIO` is kept only for spare `gpio-keys` nodes».

### 2.3 `scripts/fetch-sources.sh:5-11, 90-93` и сообщение в `ensure_uboot_tree` (~:319-329)

Написано: «the release tarballs omit the `dts/upstream` subtree».

Факт (локально, 2026-10-29): архив `u-boot-2026.10.tar.gz` (46 703 936 B,
префикс `u-boot-v2026.10/`, т.е. GitHub-архив тега) **содержит** `dts/upstream`
(14 412 записей, включая `dts/upstream/src/arm64/amlogic/meson-sm1.dtsi`);
в дереве нет `.gitmodules`, т.е. в v2026.10 `dts/upstream` вендорен в репозиторий,
а не подключён сабмодулем.

Предложение: переписать комментарий на «the script requires `dts/upstream`;
git clone is the default because it is guaranteed to contain it, a tarball is
accepted when it ships the subtree (v2026.10 does)».

Осторожно: вывод сделан по локальному кэшу архива, без сетевой проверки
других источников (`ftp.denx.de`, `source.denx.de`).

### 2.4 `docs/uboot.md:3` - статус устарел

Написано: «конфигурация и DTS подготовлены, **на железе не проверялись**».

Факт: U-Boot собирается (`out/uboot/u-boot.bin` 828 384 B, `u-boot.dtb`
75 472 B, `u-boot-nodtb.bin` 752 912 B, `U_BOOT_VERSION_NUM 2026.10`), в
defconfig есть `CONFIG_AML_USB_BURN=y`, в DTS включены `&usb` (peripheral) и
`&dwc2`.

Заменить на: «конфигурация, DTS и сборка проверены на хосте; на железе не
запускалось. Burn-гэджет: `docs/uboot-burn-port.md`».

### 2.5 `docs/uboot.md:170` - «реальная сборка не проверена»

Написано: «Что НЕ проверено: реальная сборка `make yandex-station-max_defconfig`
+ `make`».

Факт: сборка выполнена 2026-10-29 (`mainline/out/uboot/`, см. 2.4).

### 2.6 `docs/uboot.md` (defconfig/DTS-разделы) - не отражает burn-порт

В `mainline/u-boot/yandex-station-max_defconfig` есть `CONFIG_AML_USB_BURN=y`,
`CONFIG_USB_GADGET*`, `CONFIG_MESON_G12A_USB_PHY=y`, `CONFIG_DM_USB_GADGET=y`;
в `mainline/u-boot/meson-sm1-yandex-station-max.dts` - `&usb { dr_mode =
"peripheral" }` и `&dwc2 { status = "okay" }`; драйвер добавляется
`patches/uboot/0001-aml-usb-burn.patch`. В `uboot.md` этого нет.

Добавить ссылку на `docs/uboot-burn-port.md` §2 и §7.

### 2.7 `docs/dts-review.md:85` - «no driver yet» для `panel@0`

Написано: «no driver yet (`gowin_led_device` + `quasar_led_screen` в вендорском
дереве)».

Факт: драйвер есть - `drivers/video/fbdev/atrios-ledpanel.c`,
`patches/linux/0005-atrios-ledpanel.patch`, `CONFIG_FB_ATRIOS_LEDPANEL=y`,
символы `ledpanel_*` в `out/linux/build/System.map` (25 совпадений);
описание - `docs/ledpanel-driver.md`. Не портированы JTAG/bitstream
(`ledpanel-driver.md:57`).

Заменить: «driver: `atrios-ledpanel` (0005); JTAG/bitstream reconfig not ported».

### 2.8 `docs/dts-review.md:86` - `gpio-keys` vs `atrios-keypad`

Написано: «`gpio-keys` ... The alternative from `docs/buttons.md`
(`yandex,gpio-keypad`, patch 0006) is not used here».

Факт: наоборот - в DTS узел `/keypad` `compatible = "yandex,gpio-keypad"`
(строки 105-115 DTS), `gpio-keys` отсутствует.

Заменить строку на «`yandex,gpio-keypad` (`atrios-keypad`, patch 0006) is the
model in the DTS; no `gpio-keys` node».

### 2.9 `docs/dts-review.md` §5, последняя строка - конфликт уже разрешён

Написано: «Exactly one of the two models may be in the tree - if the owner picks
`atrios-keypad`, the `gpio-keys` node must go».

Факт: в дереве ровно одна модель (`keypad`), `gpio-keys` нет. Строку можно
оставить как историю, но пометить «resolved 2026-10-28».

### 2.10 `docs/rotary.md:107` - «no kernel build was run»

Написано: «Patch only: no kernel build was run in this environment».

Факт: полная сборка ядра выполнена, драйвер вкомпилирован:
`rotary_volume_*` в `out/linux/build/System.map` (8 совпадений) и в
`Image.gz` (`CONFIG_INPUT_ROTARY_VOLUME=y` в `configs/atrios.config:102`).

### 2.11 `docs/buttons.md:228-231` - «The current mainline DTS still contains a `gpio-keys` node»

Факт: не содержит (`grep -n "gpio-keys" dts/meson-sm1-yandex-station-max.dts` →
пусто; DTB: только `keypad`). Абзац про DT-конфликт помечен как решённый или
удалён.

### 2.12 `docs/hardware-inventory.md:32` - compatible панели

Написано: `ya,led_screen` (binds `gowin_led_device`), `atri,led-panel`.

Факт: DTS и драйвер используют `yandex,led-screen` (+ `atri,led-panel`),
`dts/meson-sm1-yandex-station-max.dts:621`, собранный DTB:
`panel@0 compatible = "yandex,led-screen", "atri,led-panel"`.
`ya,` - не зарегистрированный префикс (это же отмечено в
`dts-review.md:135`).

### 2.13 `docs/ring.md:75` - битая ссылка на evidence

Написано: `docs/evidence/dtb-summary.json` → фактически
`mainline/docs/evidence/dtb-summary.json` не существует; в каталоге 7 файлов
(`ring-artifact.json`, `is31fl-artifact.json`, `burn-*`, `screen-*`, `wifi-*`).

Заменить на существующий `docs/evidence/ring-artifact.json` (или создать
`dtb-summary.json`, если он задуман).

### 2.14 `docs/image.md:347-352` - причина, по которой образ промежуточный

Написано (§7.8): «Нет extlinux/bootmenu, нет USB-gadget burn, нет новых фиксов
MMC/USB. ... Полный mainline-образ будет собираться отдельно, после порта
U-Boot 2026.10».

Факт: порт U-Boot сделан и собирается (`out/uboot/u-boot.bin`,
`CONFIG_AML_USB_BURN=y`), burn-протокол реализован (`docs/uboot-burn-port.md`).
Образ по-прежнему промежуточный, но по другой причине: нет сборки FIP
(BL2/BL31, `gxlimg`/`amlogic-boot-fip`), нет проверки на железе.

Переформулировать: «BL33 - вендорский, потому что FIP с нашим U-Boot ещё не
собирается и не проверен; burn-гэджет в нашем U-Boot уже есть».

### 2.15 `docs/uboot-burn.md` (шапка, строка 3) - нет ссылки на реализацию

Документ описывает протокол как «восстановлено по исходникам ... план порта»,
но реализация уже есть: `docs/uboot-burn-port.md` (ссылается на этот файл
односторонне). Добавить в шапку: «Реализация: `docs/uboot-burn-port.md`».

### 2.16 `docs/audio.md:25-40` - таблица статусов читается как «драйверов нет»

В таблице для `es8156`/`es7210`/`sy6045s` в колонке «Mainline 7.3 driver»
стоит `none`; это верно только про upstream. Фактически драйверы есть
(`patches/linux/0007-0009`, модули в `modules.tar`) и карта смёржена в DTS.
Спорно: возможно, достаточно сноски «(upstream); for this port see
`docs/codec-*.md`» - решает владелец.

### 2.17 `docs/porting-plan.md` - статусы блоков не обновлены

План писался до сборки. Сейчас: Wi-Fi stage (a) уже в дереве
(`patches/linux/0010`), аудио-карта смёржена, U-Boot собирается, burn
реализован. Либо обновить статусы, либо сослаться на `README.md#status`
как на единственный источник статусов.

## 3. Что исправлено в `README.md` (2026-10-29, владелец файла - этот аудит)

- дерево файлов: добавлены `configs/`, `firmware/`, `overlay/`, `out/`,
  `patches/linux` vs `patches/uboot`, `docs/INDEX.md`;
- «five scripts» → шесть (`fetch-sources`, `apply`, `build-kernel`,
  `build-uboot`, `build-all`, `make-image`) + `lib.sh`;
- в таблицу статусов добавлены строки **U-Boot**, **USB burn**, **Image**;
  уточнены audio (карта смёржена, DTB собирается), Wi-Fi (stage (a) в дереве,
  модуль не в `modules.tar`), screen (JTAG/bitstream не портированы),
  buttons (единственная модель в DTS, checkpatch 0 errors), ALS (upstream id);
- убрано устаревшее «A full build has not been run in this repository yet» -
  вместо него фактическое состояние хостовой сборки с размерами артефактов;
- «all nine patches» → `0002-0009` + отдельно `0010` (с записью в
  `.atrios-mainline-patches` только для 0002-0009);
- добавлены раздел **Quick start**, раздел **Final image (not built yet)** с
  требованием «без вендорского загрузчика» и ссылка на этот INDEX;
- исправлено утверждение про `dts/upstream` в тарболе (см. §2.3);
- в «Verified» добавлены: `u-boot.bin`/`u-boot.dtb`/`u-boot-nodtb.bin`,
  `AML_USB_BURN=y`, состав DTB (включая аудио-карту), `rtw88_8822es.ko`
  собирается отдельно, test-образ и его sha256.

`device/yandex/station_max/upgrade/README.md` - **правок не потребовалось**:
все 5 размеров и sha256 совпадают с файлами (проверено `sha256sum`,
2026-10-29), включая `bootloader.img` `69766dde…45e6`, `u-boot.bin.sd.bin`
`b63e9d69…7850`, `platform.conf` `1981261f…7033`, `aml_sdc_burn.ini`
`e8d93f46…a5cc`, `logo.PARTITION` `9ec538e5…239b`.

## 4. Спорное / требует проверки (вопросы, а не утверждения)

1. **Идемпотентность `apply.sh` для U-Boot не подтверждена на этой машине.**
   В `/home/leftymods/mainline-src/u-boot-2026.10` есть пропатченные файлы
   (`drivers/usb/gadget/aml_burn/*`, `config AML_USB_BURN` в Kconfig), но нет
   маркера `.atrios-mainline-patches`. Значит патч, возможно, применён вручную.
   Проверить: `scripts/apply.sh --uboot-only --dry-run` и повторный прогон.
2. **`patches/linux/0010` не записан в маркер.** `.atrios-mainline-patches`
   (mtime 07:47) содержит только 0002-0009, `patches/linux/0010-...` (07:50)
   уже применён к дереву (`drivers/.../rtw8822es.c` присутствует). Повторный
   `apply.sh` попробует применить его к пропатченному дереву - проверить, что
   `patch -R --dry-run` детектит его как применённый.
3. **Сборка ядра после 0010 не проверялась целиком.** `rtw88_8822es.ko`
   собран подкаталожно (`docs/wifi-port.md` §3), полный `make Image.gz`
   с новым символом не запускался; `CONFIG_RTW88_8822ES` в
   `configs/atrios.config` нет (включён только в `out/linux/build/.config`).
4. **`modules.tar` и test-образ не содержат `rtw88_8822es.ko`** (собраны
   раньше) - если образ нужен с Wi-Fi stage (a), `modules.tar` надо пересобрать.
5. **`dtbs_check` не прогонялся** (`dts-review.md` §0, §4.9) - перед
   upstream-отправкой обязателен.
6. **Префиксы `yandex,`/`atri,` не зарегистрированы** в
   `vendor-prefixes.yaml` - понадобится патч, иначе `dtbs_check` ругается.
7. **`docs/uboot.md:130-131`** сравнивает вендорский `u-boot.bin`
   (1 613 168 B) с `bootloader.img`; наш `u-boot.bin` - 828 384 B. Числа
   корректны для вендора, но читаются как про нашу сборку - уточнить
   формулировку.
8. **Финального образа нет, ни один документ его не обещает** - проверено:
   единственный артефакт - `out/images/atrios-mainline-test-v130.img`
   (вендорский BL33). Требование «без вендорского загрузчика» зафиксировано
   в `README.md` (раздел Final image).
9. **`patches/uboot/0001` собирается только в `u-boot.bin`; FIP не собирается.**
   `u-boot.bin.sd.bin`/`bootloader.img` по-прежнему вендорские - см.
   `docs/uboot.md:130` и `docs/uboot-burn-port.md:370+`.

## 5. Минимальный чек-лист при следующем аудите

```sh
cd /home/leftymods/atrios-mainline
git log --oneline -5
find mainline -type f -not -path '*/out/*' | sort      # дерево
mainline/scripts/{fetch-sources,apply,build-kernel,build-uboot,build-all,make-image}.sh --help
ls -l mainline/out/linux mainline/out/uboot mainline/out/images
fdtget -l mainline/out/linux/meson-sm1-yandex-station-max.dtb /   # 72 led@, keypad, sound
grep -c 'led@' mainline/dts/meson-sm1-yandex-station-max.dts      # 72
grep -n 'aml_burn' mainline/out/uboot/build/u-boot.map | head -1   # гэджет в образе
cd device/yandex/station_max/upgrade && sha256sum -c <<'SUMS'      # ассеты
69766ddefd7949dc82252422b8e8adc5fbb904947bf16191ef8e4048a40345e6  bootloader.img
b63e9d693dce13d48362f4af1fd5dbf4b37e11a8345c77b9a244550db74a7850  u-boot.bin.sd.bin
1981261f893f6cf6c6c47ef5741b4d979ba926b126f238ca21e5ecd4c5467033  platform.conf
e8d93f46803e26ce8457f902edd348806cbb88c8ef706deea2eb441879f0a5cc  aml_sdc_burn.ini
9ec538e55aba518905a17741dade6ad626f2af7a17dc89a6380313c139e6239b  logo.PARTITION
SUMS
```
