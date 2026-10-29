# Промежуточный test-образ: вендорский BL33 + mainline Linux 7.3

Статус: **собран и проверен на хосте, на железе не запускался**.
Дата: 2026-10-29. Ветка: `mainline`. Плата: Yandex Station Max (SM1 / S905X3).

Это **не релизный образ**. Его задача — дать возможность проверить драйверы
Linux 7.3 на реальной колонке до того, как будет готов mainline U-Boot.
Загрузчиком остаётся вендорский BL33 U-Boot 2015.01 (тот же `v112` из
`atrios-pack`), ядро, DTB и модули — наши.

```
образ:  mainline/out/images/atrios-mainline-test-v130.img
размер: 1 571 973 256 B (1.464 GiB)
sha256: 209bc24411305c91e6873ac2dfae226bf1b254dfae575f8be5c492b113cd4da0
```

Рядом лежит `atrios-mainline-test-v130.img.sha256` (имя файла без пути,
пригодно для `sha256sum -c` из каталога `out/images`).

## 1. Что внутри

| item | размер | источник / примечание |
|---|---:|---|
| `DDR.USB` | 1 453 936 | вендорский, байт в байт |
| `UBOOT.USB` | 1 453 936 | вендорский, байт в байт |
| `bootloader.PARTITION` | 1 453 936 | вендорский BL33, `U-Boot 2015.01` |
| `aml_sdc_burn.UBOOT` | 1 454 448 | вендорский, для SD-прошивки |
| `aml_sdc_burn.ini` | 602 | вендорский |
| `platform.conf` | 155 | вендорский |
| `logo.PARTITION` | 1 338 688 | вендорский |
| `_aml_dtb.PARTITION` | 61 440 | **пересобран** из нашего DTB (см. §3) |
| `meson1.dtb` | 61 440 | то же |
| `meson1_ENC.dtb` | 61 440 | то же |
| `boot.PARTITION` | 15 761 408 | **новый** (см. §2) |
| `system.PARTITION` | 1 551 892 480 | Buildroot rootfs + модули 7.3 |

`ampack verify` показывает 17 элементов (12 файлов + 5 `VERIFY/*` для
`PARTITION`-элементов), образ проходит проверку SHA1 всех элементов.

### 1.1 `boot.PARTITION`

Формат — старый Android boot image, который разбирает
`bl33/common/cmd_imgread.c` (`struct andr_img_hdr`, 608 байт) и
`bootm.c` (`CONFIG_ANDROID_BOOT_IMAGE`, `android_image_get_kload()`):

| смещение | поле | значение |
|---|---|---|
| `0x000` | magic | `ANDROID!` |
| `0x008` | `kernel_size` | 15 699 393 (`Image.gz`, gzip) |
| `0x00c` | `kernel_addr` | `0x20000000` |
| `0x010` | `ramdisk_size` | 0 |
| `0x014` | `ramdisk_addr` | `0x01000000` |
| `0x018` | `second_size` | 57 965 (raw FDT) |
| `0x01c` | `second_addr` | `0x00f00000` |
| `0x020` | `tags_addr` | `0x00000100` |
| `0x024` | `page_size` | 2048 |
| `0x028` | `kernel_version` | 1 (в вендорском образце тоже 1) |
| `0x02c` | `os_version` | 0 |
| `0x040` | `cmdline[512]` | 307 B, вендорский `cmdline.txt` |
| `0x240` | `id[8]` | нули, как в образце |
| `0x660` | `recovery_dtbo_size/offset`, `header_size` | 0, 0, 1648 |
| `0x800` | kernel | `Image.gz`, выровнен на 2048 |
| `0xef9800` | second stage | наш `meson-sm1-yandex-station-max.dtb` |

Кернел: 15 699 393 B gzip → 44 542 464 B, `image_size` из заголовка
45 350 912 B. Диапазон загрузки `0x20000000..0x22b40000` — 2 MB aligned,
не пересекается ни с boot-образом U-Boot (`loadaddr=0x03080000`), ни с
`secmon` (`0x05000000..0x07300000` из нашего DTB).

**Почему `kernel_addr` не как в вендорском образце.** В образце
`kernel_addr=0x01080000`, но вендорское ядро весит 29 MB и заканчивается
ниже `loadaddr=0x03080000`. Наше ядро 45 MB: при загрузке в `0x01080000`
оно перекрыло бы boot-образ, который U-Boot держит в памяти, а
`android_image_need_move()` переселяет образ только при расстоянии
меньше 32 MB (у нас ровно 32 MB). Поэтому адрес выбран выше boot-образа и
выше `secmon`. Скрипт проверяет 2 MB-выравнивание и пересечения и
отказывается собирать образ при конфликте; адрес можно переопределить
`--kernel-addr`.

**Про `header_version`.** В задаче было сказано «header_version 0». В
вендорском `boot.PARTITION` поле по смещению `0x28` равно 1; в старом
заголовке U-Boot это `kernel_version`, а не `header_version`. Мы
повторяем образец (1) и пишем полноценный v1-tail с `header_size=1648`,
как в образце. Для `bootm` это поле не используется.

### 1.2 `system.PARTITION`

* Buildroot rootfs из `/home/leftymods/rootfs-backup-20261027.tar`
  (2975 записей, собран под 4.9.113), распакован с сохранением uid/gid.
* Сверху распакован `out/linux/modules.tar`:
  `/lib/modules/7.3.0-atrios/` — 1678 `.ko`, `modules.dep` на 1678 строк.
* Старое дерево `/lib/modules/4.9.113/` **не удалено** — fallback не
  ломаем.
* Добавлен `/init -> sbin/init`: вендорский cmdline содержит
  `init=/init`, а в Buildroot-дереве есть только `/sbin/init`.
* Добавлен маркер `/etc/atrios-test-image` (ядро, загрузчик, дата).
* `depmod -b <rootfs> 7.3.0-atrios` выполнен на хосте (kmod 34.2).
* `make_ext4fs -C <fs_config> -l 1551892480 -a system`. Без `-C`
  `make_ext4fs` применяет встроенный Android `fs_config` и портит
  rootfs: `/bin/busybox` теряет setuid и получает gid 2000,
  `/etc/shadow` становится 0644. С `-C` права и владельцы сохранены
  (`busybox` 4755 root:root, `shadow` 0600, `dbus-daemon-launch-helper`
  4750 root:101).

Версия модулей — **`7.3.0-atrios`**, а не `7.3.0`: в
`configs/atrios.config` задан `CONFIG_LOCALVERSION="-atrios"`. Скрипт
определяет версию из `modules.tar` автоматически.

## 2. Как вендорский U-Boot выбирает DTB (важно)

В собранном BL33 `DTB_BIND_KERNEL` не определён, поэтому
`board_late_init()` выполняет `store dtb read ${dtb_mem_addr}`, то есть
читает DTB **из раздела `dtb`** (`_aml_dtb`/`meson1`), а не second stage
из `boot.PARTITION`. Затем `bootm` подставляет этот blob ядру
(`CONFIG_ANDROID_IMG`, `image-fdt.c`).

Если оставить в разделе `dtb` вендорский DTB, Linux 7.3 получит
device tree от 4.9, и наши драйверы не поднимутся. Поэтому
`make-image.sh`:

1. берёт 2048-байтовый заголовок вендорского AML_ v2-контейнера
   (три 16-байтовых id, по которым `checkhw()` находит
   `yandex-station-2g`);
2. меняет в нём offset/размер полезной нагрузки;
3. кладёт внутрь наш `meson-sm1-yandex-station-max.dtb`;
4. пишет одинаковый контейнер в `_aml_dtb.PARTITION`, `meson1.dtb`,
   `meson1_ENC.dtb`.

Проверено `dtbSplit`: контейнер разбирается, id `yandex-station-2g`,
payload 57 965 B побайтово равен нашему DTB. Вендорские файлы в
`atrios-pack/tools/fw_builder/staging/` при этом не изменяются — всё
делается в нашем staging.

Второй stage в `boot.PARTITION` тоже содержит наш DTB: он остаётся
рабочим fallback, если на конкретной сборке BL33 всё-таки включён
`DTB_BIND_KERNEL`.

## 3. Сборка

```sh
cd /home/leftymods/atrios-mainline
mainline/scripts/make-image.sh
```

Артефакты:

```
mainline/out/images/atrios-mainline-test-v130.img
mainline/out/images/atrios-mainline-test-v130.img.sha256
mainline/out/images/staging/           # можно удалить, пересоберётся
```

Опции:

| опция | по умолчанию |
|---|---|
| `--out DIR` | `mainline/out/images` |
| `--staging DIR` | `<out>/staging` |
| `--rootfs TAR` | `/home/leftymods/rootfs-backup-20261027.tar` |
| `--kernel FILE` | `mainline/out/linux/Image.gz` |
| `--dtb FILE` | `mainline/out/linux/meson-sm1-yandex-station-max.dtb` |
| `--cmdline FILE` | `atrios-pack/tools/fw_builder/overlay/boot/cmdline.txt` |
| `--kver VER` | авто из `modules.tar` (`7.3.0-atrios`) |
| `--kernel-addr ADDR` | `0x20000000` |
| `--no-rootfs` | использовать `staging/system.PARTITION` из atrios-pack |
| `--fakeroot` | авто, если не root и есть `fakeroot` |

Переменные: `ATRIOS_PACK`, `VENDOR_STAGING`, `VENDOR_TOOLS`, `ROOTFS_TAR`,
`MODULES_TAR`, `SYSTEM_SIZE`, `VERSION`, `KERNELRELEASE`, `KERNEL_ADDR`.
`ATRIOS_NO_FAKEROOT=1` отключает fakeroot (владельцы файлов станут
текущим uid/gid).

Требования: `python3`, `tar`, `sha256sum`, `fakeroot` (желательно),
`depmod`/`debugfs` (опционально), `ampack`, `make_ext4fs` из atrios-pack.

`--no-rootfs` проверен отдельным прогоном: образ собирается, но в нём
нет модулей 7.3 (используется готовый Buildroot `system.PARTITION`).

Образ **не бит-репроизводим**: `make_ext4fs` записывает в inode текущее
время, поэтому sha256 меняется от прогона к прогону. `boot.PARTITION` и
dtb-контейнер детерминированы.

## 4. Выполненные проверки

Прогон 2026-10-29:

1. `ampack pack --out-ver v2 --verify` → `Packed ...`, все элементы
   проверены.
2. `ampack verify` → `Verified image at '...'`, 17 элементов.
3. `ampack unpack` → 12 файлов; `cmp` staging/распакованное совпадает.
4. Разбор `boot.PARTITION` (встроенный парсер в скрипте):
   `kernel_size = 15 699 393` = размер `Image.gz`, kernel bytes совпадают;
   `dtb_size = 57 965` = размер DTB, байты совпадают, offset `0xef9800`;
   `page_size=2048`, `kernel_version=1`, `header_size=1648`.
   `kernel_addr=0x20000000`, 2 MB aligned, с `secmon` не пересекается.
5. `dtbSplit _aml_dtb.PARTITION` → `Found header: yandex-station-2g,
   offset: 2048, size: 57965`, payload `cmp`-равен mainline DTB.
6. `mount -o loop,ro system.PARTITION`:
   `/lib/modules/7.3.0-atrios` (1678 `.ko`), три наших кодека
   (`snd-soc-es7210.ko`, `snd-soc-es8156.ko`, `snd-soc-sy6045s.ko`),
   `/lib/modules/4.9.113` на месте, `/init -> sbin/init`,
   `busybox` 4755, `shadow` 0600.
7. `e2fsck -fn`: ФС читается, но `make_ext4fs` оставляет
   `Padding at end of inode bitmap is not set` — известная косметика
   make_ext4fs; ядро монтирует ФС, `dumpe2fs` показывает `clean`.
8. `sha256sum -c`: файл `.sha256` совпадает с образом.

`ampack` не умеет разбирать Android boot image (это упаковщик Amlogic v2).
Отдельного `bootimg`/`unpack_bootimg` в окружении нет, `imgextractor.py`
работает только с ext4. Поэтому boot-заголовок проверяется нашим
парсером (`verify_boot_partition`), а поля сверены с
`bl33/common/cmd_imgread.c`, `bl33/common/bootm.c` и
`bl33/common/image-android.c`.

## 5. Прошивка

### 5.1 USB Burning Tool (Windows)

Импортировать `atrios-mainline-test-v130.img` как обычный Amlogic v2
пакет и нажать Start. Пакет содержит `bootloader.PARTITION`, поэтому
тул запишет и BL33 (тот же вендорский `v112`, что и в рабочих
кастомных образах). Это осознанно: цель — проверить ядро, а не менять
загрузчик.

`aml_sdc_burn.ini` в пакете: `erase_bootloader=1`, `erase_flash=0`.
Не включайте erase flash без необходимости.

### 5.2 SD-карта (aml_sdc_burn)

FAT32-карта, в корне:

```
aml_sdc_burn.UBOOT
aml_sdc_burn.ini
platform.conf
aml_upgrade_package.img   # наш .img, переименованный (имя задано в ini)
```

Стандартная процедура Amlogic SD-burn: выключить, вставить карту,
включить (при необходимости удерживая кнопку/Reset — см. процедуру
`atrios-pack`). Этот путь работает через BootROM и остаётся спасательным
кругом, если BL33 в eMMC повреждён.

### 5.3 Что не пишется

Пакет не содержит `vbmeta`, `vendor`, `odm`, `product`, `env`, `misc`.
Существующие разделы на устройстве остаются как есть. Это важно для AVB
(см. §7).

## 6. Что проверить на железе

Консоль: `ttyS0`, 115200 8N1 (`console=ttyS0,115200`).

Последовательность:

1. **U-Boot log**: `U-Boot 2015.01`, `Multi dtb tool version: v2`,
   `Find match dtb`, `load dtb from 0x1000000`, `Starting kernel ...`.
   Если видно `Not match any dtb` и scan — контейнер всё равно может
   подойти, но проверьте §7.1.
2. `uname -r` → `7.3.0-atrios`.
3. `cat /proc/device-tree/model` → `Yandex Station Max`. Это главный
   признак, что ядро получило **наш** DTB, а не вендорский.
4. `cat /proc/cmdline` → `root=/dev/mmcblk0p18 ... console=ttyS0,115200`.
5. `mount | grep ' / '` → root с `mmcblk0p18`; `dmesg | grep -i mmc`.
6. `dmesg | grep -iE 'meson|secmon|psci'` — нет panic/ошибок reserved
   memory.
7. Кодеки:
   ```sh
   modprobe snd-soc-es7210
   modprobe snd-soc-es8156
   modprobe snd-soc-sy6045s
   dmesg | tail -40
   aplay -l
   ```
   Штатный `/etc/init.d/S01modules` на boot пытается грузить модули из
   `/lib/modules/4.9.113` — они ожидаемо падают с `Invalid module
   format`. Наш `depmod` уже есть, `modprobe` по имени работает.
   Подписей модулей нет (`CONFIG_MODULE_SIG` off), vermagic наших `.ko` —
   `7.3.0-atrios SMP preempt mod_unload aarch64`, совпадает с ядром.
8. `cat /proc/asound/cards`, `ls /dev/fb*`, `ls /sys/class/leds`,
   `ls /sys/bus/i2c/devices`.
9. `i2cdetect -y 0/1/2` (если есть в rootfs) — ES7210 0x40, ES8156
   0x08, SY6045S 0x2a/0x2b, LED ring 0x3c/0x3f.

## 7. Риски и что делать

### 7.1 DTB: вендорский U-Boot может взять свой blob

Мы пересобрали раздел `dtb`, но если конкретная сборка BL33 читает DTB
не оттуда, ядро получит вендорский DTB. Признак — `model` не
`Yandex Station Max` и пустые `soc`-узлы в `dmesg`.
Проверить: `cat /proc/device-tree/model`, `ls /proc/device-tree/soc`.
Если DTB не наш — на железе проверить, включён ли `DTB_BIND_KERNEL` в
этой сборке BL33, и/или залить наш DTB в раздел `dtb` вручную
(`dd`/`emmc dtb_write`). Скрипт уже кладёт DTB во все три имени.

### 7.2 Адрес загрузки ядра / перекрытие памяти

Вендорский `kernel_addr=0x01080000` для нашего 45 MB ядра перекрывает
boot-образ, а `secmon` занимает `0x05000000..0x07300000`. Мы грузим ядро
в `0x20000000` (см. §1.1), скрипт это проверяет. Если на железе
загрузка зависает после `Starting kernel`, смотреть:
* последнее сообщение U-Boot (`Starting kernel ...` есть, дальше тишина
  → проблема ядра/адреса/DTB);
* попробовать `--kernel-addr 0x22000000` (2 MB aligned, тоже вне
  `secmon`) или `0x40000000` (1 GB, только для 2 GB платы);
* ни в коем случае не возвращать `0x01080000` без анализа перекрытия.

### 7.3 AVB / secure boot

`boot.PARTITION` не подписан AVB, `vbmeta` в пакет не входит. Если
устройство залочено и проверяет AVB, загрузка будет отклонена, хотя
вендорский BL33 — тот же, что в рабочих кастомных образах.
Проверить в U-Boot log: `avb2`, `verifiedbootstate`,
`dm-verity`. Если AVB включён — этот образ не для такого устройства;
нужен подписанный boot/vbmeta.

### 7.4 Userspace 4.9

Buildroot собран под 4.9.113. Ядерный ABI стабилен, базовые утилиты
заработают, но вендорские сервисы (`wifi-power`, BT H5, LED-демоны,
quasar) используют 4.9-интерфейсы и будут падать. Это ожидаемо:
образ нужен для проверки **драйверов ядра**, а не пользовательского
стека.

### 7.5 PCM-нумерация

Mainline ASoC создаёт карту с другими именами/номерами PCM, чем
вендорский `AMLAUGESOUND`. `alsactl restore`/PipeWire-конфиги из
rootfs рассчитаны на вендорские controls, поэтому звук «из коробки»
может молчать. Проверять `aplay -l`, `amixer -c 0 contents`, затем
ручной `speaker-test`. Это не дефект образа, а следующий этап порта.

### 7.6 `init=/init`

Вендорский cmdline оставлен как есть, `/init -> sbin/init` добавлен в
rootfs. Если заменить rootfs на другой — symlink нужно добавить снова
или менять cmdline на `init=/sbin/init`.

### 7.7 Нумерация mmc

`root=/dev/mmcblk0p18` требует, чтобы eMMC был `mmcblk0`. В нашем DTB
aliases: `mmc0 = &sd_emmc_c` (`ffe07000`, eMMC). Если на железе
нумерация другая, будет `VFS: Unable to mount root fs` — смотреть
`dmesg`/`lsblk` с консоли.

### 7.8 Нет mainline U-Boot

Нет extlinux/bootmenu, нет USB-gadget burn, нет новых фиксов MMC/USB.
Загрузка идёт только вендорским Android-путём (`imgread`/`bootm`).
Полный mainline-образ будет собираться отдельно, после порта
U-Boot 2026.10.

### 7.9 Wi-Fi / Bluetooth

Драйверы и firmware в mainline другие (`rtw88`, `btrtl`/`hci_h5`), чем
вендорские `88x2es`/`bt-dev`. В этом образе они, скорее всего, не
поднимутся. Это последний пункт плана порта, не цель test-образа.

### 7.10 e2fsck warning

`Padding at end of inode bitmap is not set` — косметический артефакт
`make_ext4fs`, не ошибка ФС. `dumpe2fs` показывает `clean`, ядро
монтирует. Не запускайте `fsck -y` на образе без бэкапа.

## 8. Откат

1. Прошить обратно известный рабочий образ из `atrios-pack`
   (вендорский 4.9 + Buildroot) через USB Burning Tool.
2. Если нужно вернуть только boot/system — взять `boot.PARTITION` и
   `system.PARTITION` из рабочего staging и прошить их теми же
   инструментами; BL33 при этом не трогать.
3. Если eMMC BL33 повреждён — использовать SD-карту с
   `aml_sdc_burn.UBOOT`/`aml_sdc_burn.ini` (BootROM-путь). Держите такую
   карту под рукой до первого успешного запуска.
4. Так как в образе тот же вендорский BL33 `v112`, риск «окирпичивания»
   загрузчика минимален; основной риск — ядро/DTB, а не BL33.
