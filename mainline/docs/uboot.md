# U-Boot v2026.10 для Yandex Station Max (S905X3 / SM1)

Статус: **собирается, артефакты и DTB проверены; на железе не запускалось.**
Дата: 2026-10-29. Ветка: `mainline`. База: U-Boot `v2026.10`
(`2026.10`, собран `aarch64-linux-gnu-gcc` 15).

Порт burn-протокола (Amlogic USB Burning Tool) описан отдельно в
`docs/uboot-burn-port.md`; этот документ — про обычную сборку и загрузку
mainline-ядра.

---

## 0. Кратко

* Исходники порта: `mainline/u-boot/{yandex-station-max_defconfig,
  meson-sm1-yandex-station-max.dts, board/amlogic/yandex-station-max/}`.
  В дерево U-Boot они кладутся `mainline/scripts/apply.sh --uboot-only`.
* Сборка: `mainline/scripts/build-uboot.sh -j 6` (форма `-j6` скриптом **не**
  принимается — только `-j 6` / `--jobs 6`).
* `u-boot.bin` — это **BL33**, не FIP. FIP (BL2/BL30/BL31 + BL33) собирается
  отдельно, см. §5.
* Ядро грузится из Android boot image в `boot_a` (или `boot`/`boot_b`) через
  `bootm`; DTB берётся из second stage того же boot image. Резервный путь —
  bootstd/extlinux из `system_a`. Подробно — §3.
* Окружение **не сохраняется**: `ENV_IS_NOWHERE` (`saveenv` — no-op), каждый
  старт с дефолтным env из defconfig. Всё, что должно переживать перезагрузку,
  задаётся при сборке, а не через `setenv`.

---

## 1. Сборка (проверено)

```sh
cd /home/leftymods/atrios-mainline
mainline/scripts/apply.sh --uboot-only     # defconfig + DTS + board-файлы
mainline/scripts/build-uboot.sh -j 6
```

Скрипт печатает, какой именно DTS собирается, — это главная проверка, что
используется наш файл, а не SoC-дефолт:

```
[+] u-boot:  /home/leftymods/mainline-src/u-boot-2026.10
[+] build:   /home/leftymods/atrios-mainline/mainline/out/uboot/build
[+] config:  yandex-station-max_defconfig
[+] board:   dts/upstream/src/arm64/amlogic/meson-sm1-yandex-station-max.dts
[+] cross:   aarch64-linux-gnu-gcc (15)
[+] jobs:    6
```

Артефакты (сборка 2026-10-29, `mainline/out/uboot/`):

| Файл | Размер на момент сборки | Примечание |
|---|---|---|
| `u-boot.bin` | ≈ 0.8 MB (837 096) | BL33 целиком; размер зависит от текущей ревизии burn-порта |
| `u-boot-nodtb.bin` | ≈ 0.76 MB (761 624) | код без DT |
| `u-boot.dtb` | 75 472 | sha256 `45180fa5223ca61c40e3a6fcbc75483920bead8c1c5f0294b69d1e9b53c4d29e` |

`u-boot.dtb` детерминирован (одинаковый DT → одинаковый blob); `u-boot.bin`
побайтово не воспроизводим: U-Boot вкомпилирует timestamp, а `burn/`
сейчас активно правится (см. `docs/uboot-burn-port.md`). `u-boot.bin` —
сырой бинарник BL33 (`objcopy` из ELF `u-boot`; не LZ4 и не подписан), его
и заворачивает в FIP `make-fip.sh` (§5).

Доказательства, что собран **наш** DTS:

```sh
# в build/.dtb.cmd видно source_dts := .../meson-sm1-yandex-station-max.dts
fdtget -t s mainline/out/uboot/build/u-boot.dtb / model
# -> Yandex Station Max
fdtget -t s mainline/out/uboot/build/u-boot.dtb / compatible
# -> yandex,station-max amlogic,sm1
cmp mainline/out/uboot/build/u-boot.dtb mainline/out/uboot/build/dts/dt.dtb   # identical
```

Ключевые символы в `mainline/out/uboot/build/u-boot` (`nm`): `do_bootm`,
`android_image_get_kernel`, `android_image_get_second`, `part_get_info_by_name`,
`ext4_read_file`, `bootflow_scan_first`, `button_gpio_bind`,
`dwc2_udc_probe`, `aml_usb_burn_run`, `meson_board_late_init`, `misc_init_r`,
`meson_serial_probe`.

---

## 2. defconfig: что включено и почему

`CONFIG_SYS_BOARD="yandex-station-max"`, вендор — дефолт `amlogic`
(`arch/arm/mach-meson/Kconfig`), поэтому отдельного `CONFIG_TARGET_*` нет.

| Параметр | Значение | Зачем |
|---|---|---|
| `CONFIG_ARCH_MESON` + `CONFIG_MESON_G12A` | y | профиль SM1 (как у odroid-c4/hc4) |
| `CONFIG_TEXT_BASE` / `CUSTOM_SYS_INIT_SP_ADDR` | `0x01000000` / `0x20000000` | как у SM1-бордов |
| `CONFIG_SYS_LOAD_ADDR` | `0x1000000` | `loadaddr`; boot image целиком влезает в `0x01000000..0x02000000` |
| `CONFIG_OF_UPSTREAM` + `CONFIG_DEFAULT_DEVICE_TREE` | y / `amlogic/meson-sm1-yandex-station-max` | DTS ищется в `dts/upstream/src/arm64/` |
| `CONFIG_DEBUG_UART_BASE` | `0xff803000` | uart_AO, 115200 8N1 |
| `CONFIG_MESON_SERIAL` | y | консоль через `stdout-path` |
| `CONFIG_MMC_MESON_GX`, `CONFIG_PINCTRL_MESON_G12A` | y | eMMC/SD, пинмукс |
| `CONFIG_DM_GPIO` (+ `DM_GPIO_LOOKUP_LINE_NAME`) | y | кнопки/GPIO; отдельного `GPIOLIB` в U-Boot нет — его роль играет `DM_GPIO` |
| `CONFIG_BUTTON` + `CONFIG_BUTTON_GPIO` | y | `button list`/`button summary` |
| `CONFIG_DM_REGULATOR`, `DM_REGULATOR_FIXED`, `MESON_EE_POWER_DOMAIN` | y | fixed-регуляторы и power domains SM1 |
| `CONFIG_USB` + `USB_XHCI_HCD/_DWC3` + `USB_HOST/STORAGE` | y | host-стек собран (см. §4 — на плате порт один, OTG) |
| `CONFIG_DM_USB_GADGET`, `USB_GADGET_DWC2_OTG`, `USB_GADGET_DOWNLOAD` | y | gadget для `ums` и burn |
| `CONFIG_AML_USB_BURN`, `CONFIG_MESON_G12A_USB_PHY` | y | порт burn-протокола (см. `docs/uboot-burn-port.md`) |
| `CONFIG_DOS_PARTITION`, `EFI_PARTITION` | y | читать GPT (Android-разметка) |
| `CONFIG_FS_EXT4`, `CONFIG_CMD_EXT4`, `CONFIG_FS_FAT`, `CONFIG_CMD_FAT`, `CONFIG_CMD_FS_GENERIC` | y | rootfs `system` и SD |
| `CONFIG_CMD_MMC`, `CONFIG_CMD_PART` | y | `mmc read`, `part start/size` |
| `CONFIG_BOOTSTD_DEFAULTS` | y | bootstd + extlinux/script/VBE bootmeth |
| `# CONFIG_DISTRO_DEFAULTS is not set` | — | убираем deprecated script-boot; `ARCH_MESON` его «подразумевает», поэтому выключено явно |
| `CONFIG_HUSH_PARSER` | y | нужен для `for`/`if` в `bootcmd` |
| `CONFIG_ANDROID_BOOT_IMAGE` | y | `bootm` должен понимать `ANDROID!`-образ |
| `CONFIG_USE_BOOTCOMMAND` + `CONFIG_BOOTCOMMAND` | y | загрузка без env (§3.3) |
| `CONFIG_MISC_INIT_R` | y | `misc_init_r()` из board-файла: `fdtfile`, MAC |
| `CONFIG_ENV_IS_NOWHERE` (следствие отсутствия `ENV_IS_IN_*`) | — | **`saveenv` не работает**; env только дефолтный |

Проверка «нет несуществующих символов»: `make yandex-station-max_defconfig`
проходит без предупреждений `undefined symbol`, `.config` содержит все
перечисленные опции. `GPIOLIB` и `CONFIG_TARGET_*` для meson не существуют —
их в defconfig нет.

`CONFIG_ANDROID_BOOT_IMAGE` — критичная правка: без неё `bootm` на
`boot_a` печатает `Wrong Image Format` и загрузка не идёт.

---

## 3. Как U-Boot грузит mainline-ядро

### 3.1 Что лежит на eMMC (факты)

* Разметка — GPT: текущий Linux стартует с `root=/dev/mmcblk0p18`, значит
  таблицу разделов видит ядро (GPT/MBR), её же умеет читать mainline
  `part`. На железе подтвердить `part list mmc 0`.
* `bootloader` — 4 MiB, там FIP (BL2+BL30+BL31+BL33).
* `boot_a` (~16 MiB) — Android boot image: `Image.gz` + наш DTB как
  `second stage`; собирается `scripts/make-image.sh`
  (`build_boot_partition`). Заголовок v1, `page_size=2048`,
  `kernel_addr=0x20000000`, `second_addr=0x00f00000`, cmdline внутри
  заголовка (`root=/dev/mmcblk0p18 …`).
* `system_a` — ext4 с rootfs.
* `dtb` (`_aml_dtb`/`meson1`) — контейнер Amlogic `AML_` v2: 2048-байтный
  заголовок + payload-DTB. Вендорский BL33 читает DTB именно оттуда
  (`store dtb read`).

### 3.2 Решение: DTB из `boot.PARTITION` (second stage), не из `dtb`

Выбрано **second stage внутри boot image**:

1. Это «сырой» FDT (`d00dfeed`) без контейнера — mainline U-Boot понимает его
   сразу (`boot_get_fdt()` → `android_image_get_second()` в
   `boot/image-fdt.c`).
2. DTB пишется в тот же образ, что и ядро, одним прогоном `make-image.sh` —
   версии ядра и DT гарантированно совпадают.
3. Раздел `dtb` — формат Amlogic `AML_` (заголовок 0x800, board-id, padding):
   в mainline U-Boot нет его парсера, а тащить рантайм-извлечение ради
   fallback’а не нужно. Если обновить только `dtb`, а `boot` оставить старым,
   получим рассинхрон.

Почему `bootm`, а не `booti`: в `boot_a` лежит **gzip** (`Image.gz`), а `booti`
ожидает несжатый `Image`. `bootm` с `CONFIG_ANDROID_BOOT_IMAGE` сам разбирает
заголовок, распаковывает ядро по `kernel_addr` и подставляет DTB/cmdline из
образа.

Почему не `BOOTMETH_ANDROID`: этот bootmeth требует раздел `misc` с валидным
BCB и `CONFIG_ANDROID_AB` (чтобы выбрать `boot_a`/`boot_b`). Это лишняя
зависимость от заводского содержимого `misc`; явный `bootcmd` детерминирован
и меньше. Включить `BOOTMETH_ANDROID` можно позже как отдельный fallback.

### 3.3 `bootcmd` (вкомпилирован в дефолтный env)

```
for d in 0 1; do
  mmc dev ${d};
  for p in boot_a boot boot_b; do
    if part start mmc ${d} ${p} boot_start; then
      part size mmc ${d} ${p} boot_size;
      echo Booting ${p} from mmc ${d};
      if mmc read ${loadaddr} ${boot_start} ${boot_size}; then
        bootm ${loadaddr};
      fi;
    fi;
  done;
done;
echo No Android boot image, trying bootstd;
bootflow scan
```

По шагам:

1. `d=0` — eMMC (`aliases mmc0 = &sd_emmc_c`), `d=1` — SD (`mmc1`).
2. `boot_a` → `boot` → `boot_b`: A/B-имя, затем не-слотовое (на случай
   другого образа/разметки).
3. `part start/size` берут LBA и размер из GPT; `mmc read` читает **весь**
   раздел (≤16 MiB) в `${loadaddr}=0x01000000`.
4. `bootm 0x01000000`:
   * проверяет `ANDROID!`, распаковывает `Image.gz` в `0x20000000`
     (`kernel_addr` из заголовка);
   * DTB берёт из second stage (наш DTB);
   * cmdline — из заголовка образа, к нему добавляется env `bootargs`, если
     он задан.
5. Если ни один раздел не подошёл/образ битый — `bootflow scan`
   (extlinux/script, см. §3.4). Если и там пусто — консоль U-Boot.

Память: boot image занимает `0x01000000..0x01F08000` (15 761 408 B =
15.03 MiB), ядро — с `0x20000000`, `secmon` — `0x05000000..0x07300000`.
Пересечений нет. Если ядро вырастет так, что образ перестанет влезать в
`boot_a` (16 MiB), загрузка сломается — `make-image.sh` этот лимит раздела
не проверяет, за размером надо следить.

### 3.4 Резервный путь: bootstd/extlinux

`bootflow scan` использует `BOOTMETH_EXTLINUX`/`BOOTMETH_SCRIPT`. Чтобы он
сработал, в `system_a` (ext4) нужно положить:

```
/extlinux/extlinux.conf
/boot/Image                       (распакованный: gzip -dc Image.gz > Image)
/boot/amlogic/meson-sm1-yandex-station-max.dtb
```

`extlinux` в bootstd грузит ядро через `bootm`; держите в разделе
несжатый `Image` (или FIT), чтобы не зависеть от авто-декомпрессии.

`fdtfile` уже выставляется в `misc_init_r()`:
`amlogic/meson-sm1-yandex-station-max.dtb`; путь в `extlinux.conf`/`fdtdir`
должен с ним совпадать. Важно: bootstd **не умеет** разбирать Android boot
image, поэтому `boot_a` он не найдёт — это только fallback для extlinux.

---

## 4. U-Boot DTS

`meson-sm1-yandex-station-max.dts` — отдельный DT только для U-Boot
(ядерный — `mainline/dts/meson-sm1-yandex-station-max.dts`).

| Элемент | Состояние |
|---|---|
| `/memory@0` | `0x0..0x80000000` = 2 GiB; `dram_init()` берёт размер отсюда |
| `aliases` | `mmc0=eMMC (sd_emmc_c)`, `mmc1=SD (sd_emmc_b)`, `serial0=uart_AO`, `serial1=uart_A` — как в ядерном DT |
| `chosen/stdout-path` | `serial0:115200n8` |
| `uart_AO` | okay, `uart_ao_a_pins` |
| `uart_A` | okay (BT RTL8822ES), U-Boot его не использует |
| `sd_emmc_c` (eMMC) | okay, 8 бит, HS200, `max-frequency=200 MHz`; U-Boot-only compatible `amlogic,meson-sm1-mmc` включает фазу 270° |
| `sd_emmc_b` (SD) | okay, 4 бита, `max-frequency=100 MHz`, без `cd-gpios` |
| `gpio-keys` | `micmute` = GPIOA_14 (`KEY_MICMUTE`), `function` = GPIOA_15 (`KEY_VOICECOMMAND`, вендорский `0x246`) |
| USB | glue `usb@ffe09000` — okay, `dr_mode="peripheral"`; DWC2 `usb@ff400000` — okay (burn/`ums`); DWC3 `usb@ff500000` — включён по умолчанию dtsi, но общий OTG-порт отдан peripheral, отдельного host-порта нет |
| 20V_AMPL (GPIOX_9) | **намеренно не описан** — U-Boot не должен включать усилитель |
| BT reset (GPIOX_17) | **намеренно не описан** — линией владеет ядро/BT-драйвер |

Проверки на собранном DTB:

```sh
fdtget -t x mainline/out/uboot/u-boot.dtb /memory@0 reg      # 0 0 0 80000000
fdtget -t s mainline/out/uboot/u-boot.dtb /chosen stdout-path # serial0:115200n8
fdtget -t s mainline/out/uboot/u-boot.dtb /soc/usb@ffe09000 dr_mode  # peripheral
fdtget -t x mainline/out/uboot/u-boot.dtb /soc/mmc@ffe07000 reg       # 0 ffe07000 0 800
```

`gpio status -a` на железе должен показать GPIOX_9/GPIOX_17 как входы/не
тронутые — они не описаны и U-Boot их не дёргает.

---

## 5. FIP (BL2/BL30/BL31 + наш BL33)

U-Boot upstream не собирает Amlogic FIP: `u-boot.bin` — только BL33. Готовый
FIP от другой платы (odroid-c4) брать нельзя — другой DDR/ACS.

В дереве есть отдельный сборщик: **`mainline/scripts/make-fip.sh`**. Он
собирает FIP v3 (`@AML`) и проверяет результат:

```sh
mainline/scripts/make-fip.sh
# артефакты в mainline/out/fip/:
#   u-boot.bin            FIP (bootloader.PARTITION / UBOOT.USB / DDR.USB)
#   u-boot.bin.sd.bin     FIP + 512-байтная SD-шапка
#   u-boot.bin.usb.bl2    BL2-стадия для BootROM/pyamlboot
#   u-boot.bin.usb.tpl    шаблон для Amlogic USB Burning Tool
#   u-boot.bin.sha256
```

Что делает скрипт:

1. BL2/BL30/BL31 (+ `acs.bin`, `bl301.bin`) и DDR-fw берёт из
   `amlogic-boot-fip/khadas-vim3l` (generic SM1), подписывает
   `aml_encrypt_g12a --level v3`;
2. BL33 = `mainline/out/uboot/u-boot.bin`, сжимает lz4, кладёт в тот же
   layout (BL30/BL31 offsets и размеры совпадают с вендорским образом);
3. разбирает готовый FIP и проверяет, что lz4-BL33 разжимается **побайтово**
   в исходный `u-boot.bin`, плюс сверяет TOC/размер с вендорским
   `bootloader.img` (если он есть).

Проверено (2026-10-29): FIP **1 110 384 B** (1.06 MiB), `@AML` v3,
BL33 lz4 `431 472 B -> 837 096 B` (sha256 совпал с `u-boot.bin`); влезает
в 4 MiB `bootloader` (и в 2 MiB burn-лимит). `u-boot.bin.sd.bin` —
1 110 896 B.

Для сравнения, вендорский образ:

| Файл | Размер | Что это |
|---|---|---|
| `device/.../upgrade/bootloader.img` | 1 453 936 | FIP v3, `@AML`-заголовок с offset `0x10`; 1-я компонента (BL2) — тоже в v3-конверте |
| `device/.../upgrade/u-boot.bin.sd.bin` | 1 454 448 | 512-байтная SD-шапка + тот же FIP (`cmp <(dd … skip=1) bootloader.img` → identical) |

Вендорские компоненты при необходимости извлекаются:

```sh
bootloader/uboot-repo/fip/stool/parse_uboot.sh -s g12a \
    -i device/yandex/station_max/upgrade/bootloader.img -o /tmp/fipout
# -> bl2_new.bin (bl2+acs), bl30_new.bin, bl31.img, bl33.bin (LZ4C), ddr*.fw
```

Ограничения и риски:

* **BL2 — generic SM1**, а вендорский BL2 собран под конкретную LPDDR4
  Station Max (`.../yandex_station_v1/firmware/timing.c`). Совпадение
  layout/TOC ещё не значит, что DDR инициализируется. Это риск №1
  аппаратной проверки. Если не заведётся — собрать FIP с вендорским BL2
  (разделить `bl2_new.bin` на `bl2.bin` 57344 B + `acs.bin` 4096 B,
  положить рядом с вендорскими `bl30.bin`/`bl301.bin`/`bl31.img`, DDR-fw
  и `aml_encrypt_g12a` и передать этот каталог в `make-fip.sh --src DIR`;
  BL33 остаётся наш).
* v3-конверт нашего FIP не побайтово равен вендорскому (другой ключ/соль
  у `aml_encrypt_g12a`); если на устройстве прожиганы efuse secure boot,
  BL2 может отвергнуть образ. Приватных ключей устройства в репозитории нет.
* Поэтому первый запуск — **не запись**, а USB-загрузка (pyamlboot,
  `u-boot.bin.usb.bl2`/`.tpl` или `doc/board/amlogic/boot-flow.rst`).
* Перед записью снять дамп первых 4 MiB и проверить `@AML` в вендорском
  образе, чтобы не перепутать смещение (0 vs 512).

---

## 6. Прошивка

1. **Ничего не писать, пока не снят дамп** первых 4 MiB eMMC/SD и не
   проверено, где лежит вендорский FIP (`@AML` = `40 41 4d 4c`).
2. **SD-burn (спасательный круг):** `aml_sdc_burn.UBOOT`, `aml_sdc_burn.ini`,
   `platform.conf` и образ на FAT32-карте; BootROM подхватывает это без
   участия eMMC.
3. **Вендорский пакет обновления:** заменить в нём `bootloader.PARTITION` на
   `mainline/out/fip/u-boot.bin`; пишется в раздел `bootloader` (4 MiB).
4. **Вручную в eMMC/SD:** BootROM ищет FIP с сектора 1 (смещение 512):

   ```sh
   # 512-байтная SD-шапка отбрасывается, FIP кладётся в сектор 1
   dd if=mainline/out/fip/u-boot.bin.sd.bin of=$DEV \
      bs=512 skip=1 seek=1 conv=fsync,notrunc
   ```

   `boot0`/`boot1` не трогать. Для eMMC проверить, что вендорский апдейтер
   пишет FIP в то же место (см. дамп).
5. После прошивки консоль — uart_AO (ttyS0), 115200 8N1.

---

## 7. Переменные окружения

Env **энергонезависимый только в теории**: `ENV_IS_NOWHERE`, `saveenv` не
работает. Значения ниже — либо дефолт из сборки, либо выставлены кодом
(`misc_init_r`) и живут до перезагрузки.

| Переменная | Значение по умолчанию | Кто/что использует |
|---|---|---|
| `bootcmd` | см. §3.3 | autoboot |
| `loadaddr` | `0x1000000` (`CONFIG_SYS_LOAD_ADDR`) | `mmc read`, `bootm` |
| `fdtfile` | `amlogic/meson-sm1-yandex-station-max.dtb` | `misc_init_r()`; bootstd/extlinux |
| `bootargs` | не задан | если задан — добавляется **перед** cmdline из boot image |
| `fdtaddr` | не задан | fallback-адрес DTB для `bootm` |
| `boot_targets` | не задан | порядок bootdev для `bootflow scan` |
| `usb_burning` | не задан | вход в burn-режим (`docs/uboot-burn-port.md`) |
| `aml_burn_timeout` | `5000` (мс) | таймаут burn-цикла |
| `aml_burn_identify` | `0` | индикация в burn-режиме |
| `aml_burn_part_<name>` | не задан | ручное отображение имени на LBA:size, если нет GPT |
| `ethaddr`, `soc_rev` | из efuse/SM | `misc_init_r()` |

Пример ручной проверки без перезаписи `bootcmd`:

```
setenv bootargs 'console=ttyS0,115200 earlycon'
mmc dev 0
part list mmc 0
part start mmc 0 boot_a boot_start
part size  mmc 0 boot_a boot_size
mmc read ${loadaddr} ${boot_start} ${boot_size}
bootm ${loadaddr}
```

`setenv` действует только до reset; `saveenv` вернёт ошибку — это ожидаемо.

---

## 8. Что проверить на железе (по порядку)

1. **USB-загрузка без записи** (pyamlboot, `doc/board/amlogic/boot-flow.rst`):
   залить образ в RAM. Это проверяет DDR/BL2/BL33 без риска кирпича.
2. Консоль uart_AO 115200: строка `U-Boot 2026.10 … yandex-station-max`.
3. `bdinfo` → 2 GiB. Если меньше — `/memory`/BL2 не совпали.
4. `mmc list` → `mmc@ffe07000` (eMMC, mmc 0), `mmc@ffe05000` (SD, mmc 1).
5. `mmc dev 0 && mmc info` — 8 бит, HS200; при CRC снизить
   `max-frequency` до 52 МГц и сравнить без `amlogic,meson-sm1-mmc`.
6. `part list mmc 0` — есть `boot_a`, `system_a`, `dtb`, `misc`.
7. Ручной boot (§7): `part start/size`, `mmc read`, `bootm` — до того, как
   доверять `bootcmd`.
8. `button list` / `button summary` → `micmute` (GPIOA_14), `function`
   (GPIOA_15).
9. `gpio status -a` — GPIOX_9 (20V_AMPL) и GPIOX_17 (BT reset) не тронуты.
10. `bootflow scan` — чтение ext4 `system_a`, extlinux (если положили файлы).
11. `ums 0 mmc 0` — аварийный доступ к eMMC по USB.
12. После старта ядра: `cat /proc/device-tree/model` → `Yandex Station Max`,
    `cat /proc/cmdline` → `root=/dev/mmcblk0p18 … console=ttyS0,115200`.
13. Дамп первых секторов до/после прошивки FIP; `boot0` не тронут.

---

## 9. Что работает / что нет

| Блок | Статус |
|---|---|
| Сборка `u-boot.bin`/`u-boot.dtb`, наш DTS | **работает** (проверено) |
| Консоль uart_AO, `stdout-path` | собрано, на железе не проверено |
| eMMC (8 бит, HS200), SD (4 бита), GPT/разделы | собрано, не проверено |
| ext4/FAT, `mmc read`, `part start/size` | собрано, не проверено |
| `bootm` Android boot image + gzip + DTB из second stage | собрано, не проверено |
| bootstd + extlinux/script из `system_a` | собрано, не проверено |
| Кнопки GPIO (`button list`) | собрано, не проверено |
| USB gadget DWC2 (`ums`), burn-драйвер | собрано, не проверено |
| USB host (xHCI) | собран, но **порт один** и отдан peripheral — host не заработает |
| `saveenv` / постоянный env | **не работает** (`ENV_IS_NOWHERE`) |
| Чтение DTB из раздела `dtb` (AML_ контейнер) | **не поддерживается** (и не нужно) |
| FIP: подпись/запись | **не проверено**; ключей устройства нет |
| DDR/BL2, 20V-рейл, BT reset, BT UART, Ethernet, HDMI/audio | вне U-Boot-порта |

---

## 10. Риски и открытые вопросы

1. **DDR/BL2 — риск №1.** `make-fip.sh` берёт generic SM1 BL2 из
   `amlogic-boot-fip/khadas-vim3l`; вендорский BL2 собран под конкретную
   LPDDR4 Station Max (`timing.c`). Если DDR не поднимется — пересобрать FIP
   с вендорским BL2 (см. §5) или использовать вендорский FIP, заменив только
   BL33.
2. **Secure boot / подпись FIP.** v3-конверт нашего FIP подписан встроенным
   non-secure ключом `aml_encrypt_g12a`, а не ключом устройства; приватных
   ключей в репозитории нет. Если efuse secure boot закрыт — образ
   отвергнут. Сначала USB-загрузка/pyamlboot.
3. **Env не сохраняется.** Любая настройка через `setenv` исчезает после
   reset; менять поведение — только в defconfig/board-файле/`bootcmd`.
4. **Размер `boot_a`.** Наш boot image — 15.76 MB при разделе 16 MiB. Рост
   ядра/initramfs упрётся в размер; следить за `make-image.sh`.
5. **`loadaddr=0x01000000` и ядро `0x20000000`.** Запас ~480 MiB, но если
   кто-то поднимет `loadaddr` или будет читать больше одного раздела —
   возможно перекрытие с ядром. Не менять без пересчёта.
6. **Нет SM1-специфичного pinctrl** — используются g12a-таблицы. Для
   eMMC/SD/UART это то же, что у odroid-c4; при проблемах с SDIO/BT смотреть
   первым делом.
7. **SD без `cd-gpios`** — U-Boot считает карту присутствующей; если мешает,
   добавить `broken-cd`/`cd-gpios` после проверки схемы.
8. **USB host на плате недоступен**: общий OTG-порт принудительно переводится
   в peripheral при пробе DWC2 (нужно для burn/`ums`). Host-стек собран, но
   практического порта нет.
9. **`bootcmd` опирается на имена `boot_a`/`boot`/`boot_b`.** Если на
   конкретном устройстве разметка другая — проверить `part list mmc 0` и
   поправить `bootcmd` (или задать `aml_burn_part_*` для burn).
10. **`CONFIG_DISTRO_DEFAULTS` выключен**: `run distro_bootcmd` и старые
    distro-скрипты недоступны — только bootstd и наш `bootcmd`. Это
    осознанно; при необходимости вернуть — снять `# CONFIG_DISTRO_DEFAULTS
    is not set` и учесть, что `bootcmd` всё равно переопределён.
