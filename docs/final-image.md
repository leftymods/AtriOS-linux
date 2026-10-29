# Финальный образ: mainline U-Boot (BL33) в Amlogic FIP + mainline Linux 7.3

Статус: **собран и проверен на хосте, на железе не запускался**.
Дата: 2026-10-29. Ветка: `mainline`. Плата: Yandex Station Max (SM1 / S905X3).

В этом образе больше нет вендорского U-Boot 2015.01: BL33 — mainline
U-Boot 2026.10 с портом Amlogic USB-burn. Ядро, DTB, модули и rootfs — те же,
что в промежуточном test-образе (`docs/image.md`).

```
образ:  out/images/atrios-mainline-v130.img
размер: 1 571 493 000 B (1.463 GiB)
sha256: eeef11226489389935964a41c97d5e1d0899663b5ca78da9d509ae990632f4e9
```

Рядом лежит `atrios-mainline-v130.img.sha256` (имя файла без пути, пригодно
для `sha256sum -c` из каталога `out/images`).

## 1. Чем отличается от test-образа

| item | test (`atrios-mainline-test-v130.img`) | final (`atrios-mainline-v130.img`) |
|---|---:|---:|
| `bootloader.PARTITION` | 1 453 936, вендорский FIP v112 | **1 110 384, наш FIP** |
| `UBOOT.USB` | 1 453 936, вендорский | **1 110 384, наш FIP** |
| `DDR.USB` | 1 453 936, вендорский | **1 110 384, наш FIP** |
| `aml_sdc_burn.UBOOT` | 1 454 448, вендорский | **1 110 896, наш FIP + 512 B** |
| `boot.PARTITION` | 15 761 408 | 15 968 256 |
| `system.PARTITION` | 1 551 892 480 | 1 551 892 480 |
| BL33 | U-Boot 2015.01 | **U-Boot 2026.10** (`a54bf847…`) |

`boot.PARTITION`, `system.PARTITION`, `_aml_dtb.PARTITION`, `meson1.dtb`,
`meson1_ENC.dtb`, `aml_sdc_burn.ini`, `platform.conf`, `logo.PARTITION`
собираются тем же кодом, что и в `make-image.sh`. Отличаются только четыре
bootloader-элемента и имя образа.

## 2. FIP: что внутри `bootloader.PARTITION`

Amlogic загружает **FIP** (BL2 + BL30 + BL31 + BL33). Безопасно заменять
только BL33: BL2 содержит board-specific инициализацию DDR (LPDDR4,
`board_id`, 1392 MHz), BL30/BL31 — SCP/ATF.

| компонент | источник | примечание |
|---|---|---|
| BL2 | вендорский v112 FIP | board-specific DDR training; побайтово сохранён |
| BL30 / BL31 | вендорский v112 FIP | SCP/ATF, подписаны вместе с BL2 |
| **BL33** | **наш `out/uboot/u-boot.bin`** (837 232 B) | `@AML` v3 + LZ4, подписан non-secure ключом `aml_encrypt_g12a` |

Разметка файла:

| смещение | размер | содержимое |
|---:|---:|---|
| `0x00000` | `0x10000` | `bl2.n.bin.sig`: 16-байтовая соль, `@AML` заголовок с `+0x10`, подписанный BL2 |
| `0x10000` | `0x10000` | таблица компонентов: 4 записи `{16-байтовый тег, u64 offset, u64 size}` + DDR-таблица |
| `0x20000` | `0x3c000` | 8 DDR firmware blobs, байт в байт как в v112 |
| `0x5c000` | `0xe570` | BL30 (entry 0) |
| `0x6a570` | `0x2b400` | BL31 (entry 1) |
| `0x95970` | `0` | BL32 (entry 2) отсутствует |
| `0x95970` | `0x69800` | **BL33 (entry 3), наш `u-boot.bin` в lz4-контейнере** |
| `0xa5980` | `0x290` | `@AML` lz4-заголовок BL33 (entry 3 offset + `0x10010`) |
| … | `0x70` | метаданные (в т.ч. время сборки) |
| … | `0x69570` | сырой lz4-блок → 837 232 B |
| `0x10f170` | | конец файла |

Таблица FIP:

```
entry 0  off 0x5c000  size 0xe570   BL30
entry 1  off 0x6a570  size 0x2b400  BL31
entry 2  off 0x95970  size 0x0      BL32 (нет)
entry 3  off 0x95970  size 0x69800  BL33
```

Последняя запись кончается на `0xff170`; ещё `0x10000` байт в конце —
фиксированный хвост, который `bootmk` не включает в таблицу (так же у
вендорского образа). FIP **1.1 MB**, это ниже лимита burn (2 MB) и ниже
4 MB раздела `bootloader`.

### 2.1 BL33

* вход: `out/uboot/u-boot.bin`, 837 232 B,
  `sha256 a54bf8477ff8dfb0a40e2cf0ddf51a66d5b3419ca91f7fc4ac6fed12cf0eecf2`;
* `aml_encrypt_g12a --bl3sig --compress lz4 --level v3 --type bl33`;
* в FIP: 431 472 B lz4 → ровно 837 232 B после распаковки;
* копия входа: `out/fip/bl33-input.bin`.

### 2.2 BL2/BL30/BL31

`make-fip.sh` берёт **точные подписанные промежуточные файлы вендорского
v112** из `/home/leftymods/fip-tmp-v112`:

| файл | размер |
|---|---:|
| `bl2.n.bin.sig` | 65 536 |
| `bl30_new.bin.enc` | 60 560 |
| `bl31.img.enc` | 178 576 |

`bootmk` вставляет `bl2.n.bin.sig` дословно, поэтому первые `0x10000` байт
нашего FIP **побайтово равны** вендорскому v112 (проверяется при сборке).
BL30/BL31 `bootmk` пересоливает при каждом прогоне, поэтому побайтовое
сравнение шифротекста не имеет смысла, но на вход подаются именно вендорские
подписанные образы, и их размеры в таблице совпадают с v112.

Без каталога `fip-tmp-v112` скрипт **отказывается** собирать релизный FIP:
потеря вендорского BL2 означает потерю board-specific DDR training. Явный
`--no-stage` пересобирает BL2/BL30/BL31 из generic SM1-блобов
`amlogic-boot-fip/khadas-vim3l` — только для экспериментов, не для прошивки
на устройство, которое важно.

## 3. Сборка

```sh
cd /home/leftymods/atrios-mainline

scripts/fetch-sources.sh
scripts/apply.sh
scripts/build-kernel.sh --jobs 6 --strict
scripts/build-uboot.sh -j 6

# FIP с нашим BL33
scripts/make-fip.sh \
    --bl33 out/uboot/u-boot.bin \
    --out out/fip

# финальный образ
scripts/make-final-image.sh \
    --bl33 out/fip/bl33-input.bin
```

`make-final-image.sh` вызывает `make-fip.sh`, затем `make-image.sh` для
`boot/system/dtb`, подменяет четыре bootloader-элемента и упаковывает образ
через `ampack pack --out-ver v2 --verify`. Промежуточный test-образ, который
попутно собирает `make-image.sh`, удаляется.

Артефакты:

```
out/fip/u-boot.bin            1 110 384 B
out/fip/u-boot.bin.sd.bin     1 110 896 B
out/fip/u-boot.bin.usb.bl2       65 536 B
out/fip/u-boot.bin.usb.tpl    1 045 360 B
out/fip/bl33-input.bin          837 232 B
out/images/atrios-mainline-v130.img
out/images/atrios-mainline-v130.img.sha256
```

`make-fip.sh` проверяет: размер ≤ 2 MB, `@AML` v3, цепочку таблицы
компонентов, lz4 round-trip (sha256 распакованного BL33 == вход) и наличие
`aml_usb_burn_run` в ELF (для `--burn`).

### 3.1 Опции `make-fip.sh`

| опция | по умолчанию |
|---|---|
| `--bl33 FILE` | `out/uboot/u-boot.bin` |
| `--out DIR` | `out/fip` |
| `--stage DIR` | `/home/leftymods/fip-tmp-v112` |
| `--no-stage` | пересобрать BL2/BL30/BL31 из `$FIP_SRC` (явно, небезопасно) |
| `--src DIR` | `amlogic-boot-fip/khadas-vim3l` |
| `--no-lz4` | BL33 без сжатия (обычно > 2 MiB, не влезает) |
| `--ddrfw N` | 8 (как в вендорском v112) |
| `--burn` | хук: взять `out/uboot/burn/u-boot.bin`, если он есть |
| `--verify FIP` | только проверить готовый FIP |

Переменные: `FIP_BL33`, `FIP_OUT_DIR`, `FIP_STAGE_DIR`, `FIP_SRC`,
`FIP_REFERENCE` (вендорский v112 для сверки), `FIP_MAX_SIZE` (2 МиБ).

`--burn` — хук на включение burn-сборки: когда отдельная сборка U-Boot с
burn-портом появится в `out/uboot/burn/u-boot.bin`, пайплайн подхватит её без
правок. Сейчас `CONFIG_AML_USB_BURN=y` уже включён в основной `u-boot.bin`;
`--burn` печатает наличие `aml_usb_burn_run` в ELF.

### 3.2 Опции `make-final-image.sh`

| опция | по умолчанию |
|---|---|
| `--fip FILE` | собрать через `make-fip.sh` |
| `--bl33 FILE` | `out/uboot/u-boot.bin` |
| `--src DIR` | передаётся в `make-fip.sh` |
| `--no-lz4`, `--burn` | передаются в `make-fip.sh` |
| `--rootfs`, `--kernel`, `--dtb`, `--cmdline`, `--kver`, `--kernel-addr`, `--no-rootfs` | передаются в `make-image.sh` |
| `--reuse-staging` | использовать готовый `out/images/.final-work/staging` |
| `--fakeroot` | авто, если не root и есть `fakeroot` |

`--reuse-staging` полезен для итераций по загрузчику: `boot/system/dtb` не
пересобираются, меняется только FIP.

### 3.3 Воспроизводимость

Образ **не бит-репроизводим**:

* `make_ext4fs` пишет в inode текущее время;
* lz4-метаданные BL33 внутри FIP содержат время сборки, поэтому sha256 FIP
  меняется от прогона к прогону даже при одинаковом `u-boot.bin`.

Сравнивать имеет смысл содержимое (`ampack unpack` + `cmp`), а не sha256.

## 4. Выполненные проверки

Прогон 2026-10-29:

1. `ampack verify` → `Verified image at '...'`, образ проходит SHA1 всех
   элементов.
2. `sha256sum -c atrios-mainline-v130.img.sha256` → `OK`.
3. `ampack unpack` → 12 файлов; `bootloader.PARTITION == UBOOT.USB == DDR.USB`
   побайтово; `aml_sdc_burn.UBOOT[512:] == bootloader.PARTITION`.
4. `scripts/make-fip.sh --verify bootloader.PARTITION --bl33 bl33-input.bin`:
   * FIP 1 110 384 B ≤ 2 097 152 B;
   * `@AML` v3, таблица компонентов непротиворечива, конец последней записи
     `0xff170` + хвост `0x10000` = размер файла;
   * BL33 lz4-контейнер на `0xa5980`, 431 472 B → 837 232 B;
   * распакованный BL33 побайтово равен `bl33-input.bin`
     (`sha256 a54bf847…`);
   * BL2 побайтово равен вендорскому v112;
   * размеры BL30/BL31 совпадают с v112 (`0xe570`, `0x2b400`).
5. `boot.PARTITION`: `ANDROID!`, `kernel_size=15 905 990` = размер
   `out/linux/Image.gz`, `kernel_addr=0x20000000`, `second_size=58 207` =
   размер DTB, DTB на `0xf2c000`, байты ядра и DTB совпадают с файлами,
   `page_size=2048`, `kernel_version=1`, `header_size=1648`.
6. `_aml_dtb.PARTITION`: контейнер `AML_` v2, payload на `0x800` побайтово
   равен `out/linux/meson-sm1-yandex-station-max.dtb`.
7. `system.PARTITION`: `/lib/modules/7.3.0-atrios` на месте, три кодека
   (`snd-soc-es7210.ko`, `snd-soc-es8156.ko`, `snd-soc-sy6045s.ko`),
   `/init -> sbin/init`, `busybox` 4755, `/etc/shadow` 0600; модуль
   `snd-soc-es7210.ko` из образа побайтово равен модулю из `modules.tar`.

Входные артефакты:

| вход | sha256 |
|---|---|
| `out/uboot/u-boot.bin` (BL33) | `a54bf8477ff8dfb0a40e2cf0ddf51a66d5b3419ca91f7fc4ac6fed12cf0eecf2` |
| `out/linux/Image.gz` | `0cdaa98ed85f70389a1355322df5f68a1a93934fe554f7af3f62626f41463df3` |
| `out/linux/meson-sm1-yandex-station-max.dtb` | `90bd8d77257d7c03f52300519b6cf75145c83d89d198a3b623f8bb25fa3b06ec` |
| `out/linux/modules.tar` | `f31503347686e54c5159f943cfd59dde4b66d965ef8024a921fac7ca8c409f23` |

FIP:

```
u-boot.bin         1 110 384 B  ad6ebe46d008c247c851d835e4e2c9deea4fbff7adb389ec261b7e7dc77b2983
u-boot.bin.sd.bin  1 110 896 B  0913d27bd855e61b3c680313d675f4b02496940eb4cc3527df1e26b4bd6f68bf
```

## 5. Прошивка

- **USB Burning Tool**: устройство должно показать `1b8e:c003` (наш burn
  gadget). Тул разбирает пакет и шлёт per-item `download`/`verify`;
  `bootloader_is_old` отвечается `failed`, поэтому `boot` не стирается до
  записи нового. `aml_sdc_burn.ini`: `erase_bootloader=1`, `erase_flash=0`.
- **SD-карта**: `aml_sdc_burn.UBOOT` (наш FIP + 512 B) + `aml_sdc_burn.ini`
  из того же пакета + `platform.conf` + образ, переименованный в имя из ini.
  FAT32, стандартная процедура Amlogic SD-burn; идёт через BootROM.

Перед первой прошивкой снять первые 4 MiB eMMC и сохранить копию вендорского
FIP v112 — это путь отката:

```
/home/leftymods/atrios-uboot2015-2019-lz4-v112/u-boot.bin        (1 453 936 B, md5 5735c86e…)
/home/leftymods/atrios-uboot2015-2019-lz4-v112/u-boot.bin.sd.bin  (1 454 448 B)
```

Откат: прошить `atrios-mainline-test-v130.img` (вендорский BL33 + наше ядро,
sha256 `209bc244…`) или собрать образ через `make-image.sh` без
`make-fip.sh`. Если загрузчик повреждён — SD-burn (BootROM не зависит от FIP
в eMMC).

## 6. Риски

1. **BL2/DDR.** BL2 — вендорский, не пересобранный из исходников (в нём
   board-specific LPDDR4 training). В текущем образе он побайтово равен v112;
   при `--no-stage` он заменяется generic-блобом, и устройство может не
   дойти до U-Boot. Восстановление — USB BootROM mode / SD-burn.
2. **Secure boot.** FIP подписан non-secure ключом Amlogic. Устройство с
   прошитым secure-boot eFuse его отвергнет. Проверить в логе: `secure boot`,
   `sig check failed`, `avb2`, `verifiedbootstate`.
3. **Нет env storage** (`ENV_IS_NOWHERE`): `bootcmd` вкомпилирован,
   `saveenv` ничего не делает. Политика загрузки — только пересборка U-Boot.
4. `boot_a` — 16 MiB, текущий boot image ~15.8 MB: запас на initramfs
   небольшой; при росте ядра/initramfs проверить размер раздела.
5. **Загрузочный сценарий mainline U-Boot.** `bootcmd` перебирает
   `boot_a`/`boot`/`boot_b` на mmc 0/1, читает `mmc read` и делает
   `bootm` по Android boot image (`CONFIG_ANDROID_BOOT_IMAGE=y`). Нужны
   совпадающие имена разделов в GPT и корректный second stage (наш DTB).
   На железе не проверялось.
6. **DTB.** В `boot.PARTITION` второй stage — наш DTB; в разделе `dtb` —
   тот же DTB в AML_ v2-контейнере. Если mainline U-Boot читает отдельный
   `dtb`-раздел, там тоже наш DTB. Проверка: `cat /proc/device-tree/model`.
7. **Адрес ядра.** `kernel_addr=0x20000000` (2 MB aligned), не пересекается
   с boot-образом (`loadaddr=0x03080000`) и `secmon`
   (`0x05000000..0x07300000`). При зависании после `Starting kernel`
   попробовать `--kernel-addr 0x22000000`/`0x40000000`.
8. **AVB/vbmeta.** Пакет не содержит `vbmeta`, boot не подписан AVB; на
   устройстве с верификацией загрузка будет отклонена.
9. **Userspace.** Rootfs — Buildroot от 4.9.113; сервисы 4.9 могут падать.
   Поверх применяется `overlay/` (asound.conf, `S01modules`,
   `S60atrios`, wifi/fb-хелперы). `S01modules` сначала пробует
   `insmod /lib/modules/4.9.113/*.ko`, затем `modprobe` (после `depmod`
   находит 7.3-модуль). Модули в `system.PARTITION` принадлежат
   `1000:1000` (так их упаковал `modules.tar`); для загрузки это не мешает.
10. **Косметика.** В rootfs остаётся маркер `/etc/atrios-test-image` от
    `make-image.sh`; в текущем образе он говорит «intermediate TEST image» и
    «vendor U-Boot 2015.01». На загрузку не влияет.
11. **Недетерминированность.** См. §3.3: sha256 образа/FIP меняются от
    прогона к прогону.

## 7. Проверка на железе

```
lsusb -v -d 1b8e:c003                 # burn gadget descriptors
dmesg | grep -i 'usb\|mmc'            # U-Boot side
cat /proc/device-tree/model           # "Yandex Station Max"
cat /proc/cmdline                     # root=/dev/mmcblk0p18 ... init=/init
mount | grep ' / '                    # root с mmcblk0p18
dmesg | grep -iE 'meson|secmon|psci'  # без panic
aplay -l; arecord -l                  # card + PCM 0/1/2
ls /sys/class/graphics/fb*/name       # atrios-ledpanel, led-ring-fb
dmesg | grep -i 'rfe_def\|rtw88'      # Wi-Fi stage (a)/(b)
```

Кодеки:

```sh
modprobe snd-soc-es7210
modprobe snd-soc-es8156
modprobe snd-soc-sy6045s
dmesg | tail -40
```

Проверка burn-режима (осторожно): `setenv usb_burning 1; reset` — плата
должна подняться как USB-устройство Amlogic. Если что-то пошло не так,
откат через SD-burn.
