# U-Boot v2026.10 для Yandex Station Max (S905X3 / SM1)

Статус: конфигурация и DTS подготовлены, **на железе не проверялись**.
База — U-Boot, тег `v2026.10` (tag object `a58e2b7a…`, проверено через GitHub API).

## 1. Что даёт апстрим

| Блок | Факт | Ссылка (u-boot v2026.10) |
|---|---|---|
| Платформа | SM1 собирается профилем `CONFIG_MESON_G12A` (так же у odroid-c4/hc4, khadas-vim3l) | `arch/arm/mach-meson/Kconfig:57-62`, `configs/odroid-c4_defconfig:13` |
| `TARGET_*` | **их нет для meson**: борд выбирается строками `SYS_VENDOR`/`SYS_BOARD`, код лежит в `board/<vendor>/<board>/` | `arch/arm/mach-meson/Kconfig:78-98` |
| Тактирование | драйвер g12a понимает `amlogic,sm1-clkc` | `drivers/clk/meson/g12a.c:1011-1013` |
| Pinctrl | SM1 использует **g12a**-совместимые таблицы; отдельного SM1-драйвера в U-Boot нет | `drivers/pinctrl/meson/pinctrl-meson-g12a.c:1276,1280`; DTS: `meson-g12-common.dtsi:271-272, 1759-1760` |
| MMC | `amlogic,meson-sm1-mmc` включает фазу `CLK_CO_PHASE_270` (стабильно на SM1 >50 МГц) | `drivers/mmc/meson_gx_mmc.c:73-74, 401` |
| UART | uart_AO матчится по второму compatible `amlogic,meson-gx-uart` | `drivers/serial/serial_meson.c:245`; `meson-g12-common.dtsi:2072-2081` |
| Объём RAM | `dram_init()` берёт размер из узла `/memory` (`reg[1]`) | `arch/arm/mach-meson/board-common.c:34-50` |
| Размещение DTS | при `CONFIG_OF_UPSTREAM=y` DT ищется в `dts/upstream/src/arm64/<vendor>/` | `dts/Makefile:11-38` |
| Bootstd | `DISTRO_DEFAULTS` помечен deprecated, вместо него `BOOTSTD_DEFAULTS`, `bootcmd = "bootflow scan"` | `boot/Kconfig:1209-1226`, `boot/Kconfig:543-551`, `boot/Kconfig:2011-2016` |
| Env | без `ENV_IS_IN_*` включается `ENV_IS_DEFAULT` → `ENV_IS_NOWHERE`, окружение **не сохраняется** | `env/Kconfig:95-110` |

Следствие: **новый `CONFIG_TARGET_*` не нужен и не создаётся.** Мы используем штатный
механизм `SYS_BOARD`, поэтому в defconfig стоит `CONFIG_SYS_BOARD="yandex-station-max"`,
а в дерево U-Boot добавляется каталог `board/amlogic/yandex-station-max/`.
Переиспользовать `board/amlogic/odroid-n2/odroid-n2.c` нельзя без правок: он читает ADC
для определения варианта Odroid и выставляет `fdtfile` вида `meson-sm1-odroid-*.dtb`
(`board/amlogic/odroid-n2/odroid-n2.c:69-105`).

## 2. Файлы в этом каталоге и куда их класть

```
mainline/u-boot/
  yandex-station-max_defconfig
  meson-sm1-yandex-station-max.dts
  board/amlogic/yandex-station-max/{Makefile,yandex-station-max.c,MAINTAINERS}
```

В дереве U-Boot (проверено, что имена/пути совпадают с v2026.10):

```sh
cd /path/to/u-boot-2026.10
cp /path/to/AtriOS/mainline/u-boot/yandex-station-max_defconfig      configs/
cp /path/to/AtriOS/mainline/u-boot/meson-sm1-yandex-station-max.dts  dts/upstream/src/arm64/amlogic/
cp -r /path/to/AtriOS/mainline/u-boot/board/amlogic/yandex-station-max board/amlogic/
```

`CONFIG_DEFAULT_DEVICE_TREE="amlogic/meson-sm1-yandex-station-max"` — путь считается
от `dts/upstream/src/arm64/`, т.е. файл обязан лежать в `.../arm64/amlogic/`
(`dts/Makefile:11-38`); `#include "meson-sm1.dtsi"` подхватывается из того же каталога.

## 3. Сборка

```sh
export CROSS_COMPILE=aarch64-linux-gnu-
make yandex-station-max_defconfig
make -j$(nproc)
```

Результат:

* `u-boot.bin` — **BL33** (payload для FIP), это то, что подписывается;
* `u-boot` — ELF, `u-boot.dtb` — вкомпилированный DT;
* `u-boot.bin.sd.bin` сборочной системой U-Boot **не создаётся** (в `Makefile`
  такого правила нет). Его делает подписывающий инструмент Amlogic
  (`aml_encrypt_g12a --bootmk`, см. `doc/board/amlogic/odroid-c4.rst`,
  `doc/board/amlogic/pre-generated-fip.rst`) либо `amlogic-boot-fip/build-fip.sh`.
  В вендорском дереве это видно в `bootloader/uboot-repo/fip/g12a/build.sh:111-118`.

Ключевые параметры defconfig (все сверены с Kconfig v2026.10):

| Параметр | Значение | Почему |
|---|---|---|
| `CONFIG_TEXT_BASE` | `0x01000000` | как у SM1-бордов (`odroid-c4_defconfig:4`); старое имя — `SYS_TEXT_BASE` |
| `CONFIG_CUSTOM_SYS_INIT_SP_ADDR` | `0x20000000` | как у odroid-c4 |
| `CONFIG_MESON_G12A` | `y` | профиль SM1 |
| `CONFIG_DEFAULT_DEVICE_TREE` | `amlogic/meson-sm1-yandex-station-max` | наш DTS |
| `CONFIG_DEBUG_UART_BASE` | `0xff803000` | регистры uart_AO |
| `CONFIG_BAUDRATE` | 115200 (default) | консоль `serial0:115200n8` |
| `CONFIG_BOOTSTD_DEFAULTS` | `y` | вместо deprecated `DISTRO_DEFAULTS` |
| `CONFIG_BUTTON` + `CONFIG_BUTTON_GPIO` | `y` | кнопки из `gpio-keys` |
| `CONFIG_FS_EXT4`, `CONFIG_DOS_PARTITION`, `CONFIG_EFI_PARTITION` | `y` | читать Android-разметку и rootfs |
| `CONFIG_MMC_MESON_GX`, `CONFIG_PINCTRL_MESON_G12A`, `CONFIG_MESON_SERIAL` | `y` | eMMC/SD, пинмукс, консоль |

## 4. DTS: что подтверждено

`mainline/u-boot/meson-sm1-yandex-station-max.dts` собран `cpp` + `dtc` (см. §6), поэтому
имена макросов и лейблов проверены фактически:

| Элемент | Подтверждено |
|---|---|
| `GPIOA_14` = 63, `GPIOA_15` = 64 | `dts/upstream/include/dt-bindings/gpio/meson-g12a-gpio.h` (в дереве U-Boot нумерация на 1 меньше вендорской, физические пины те же: вендор `0x40/0x41`) |
| `GPIOX_9` = 74, `GPIOX_17` = 82 | там же (в вендоре 75/83) |
| `&gpio`, `&gpio_ao`, `&uart_AO`, `&uart_A`, `&sd_emmc_c`, `&sd_emmc_b` | `meson-g12-common.dtsi` |
| `uart_ao_a_pins`, `uart_a_pins`, `emmc_ctrl_pins`, `emmc_data_8b_pins`, `emmc_ds_pins`, `emmc_clk_gate_pins`, `sdcard_c_pins`, `sdcard_clk_gate_c_pins` | `meson-g12-common.dtsi:309-362, 886-906, 1501, 1916` |
| `KEY_MICMUTE` (248), `KEY_VOICECOMMAND` (582) | `dt-bindings/input/input.h` + `linux-event-codes.h` |
| memory = 2 GiB (`0x80000000`) | узел `/memory` читает `board-common.c:34-50` |

Особенности:

* **aliases**: `meson-g12-common.dtsi:21-25` уже задаёт `mmc0 = &sd_emmc_b` (SD),
  `mmc1 = &sd_emmc_c` (eMMC), `mmc2 = &sd_emmc_a` (SDIO). Мы переопределяем на
  ядерную нумерацию: `mmc0 = eMMC`, `mmc1 = SD`. В U-Boot это влияет на `mmc dev N`
  и на порядок обхода bootstd.
* **`compatible = "amlogic,meson-sm1-mmc", "amlogic,meson-axg-mmc"`** в `sd_emmc_c/b` —
  U-Boot-only. Именно первый compatible включает фазу 270° для SM1
  (`meson_gx_mmc.c:73-74`); порядок разбора compatible — от первого к последнему
  (`drivers/core/lists.c:232-263`). **В ядерный DTS это добавлять нельзя** (в Linux
  такого compatible нет).
* **Кнопки** описаны обычным `gpio-keys`: U-Boot-драйвер `button-gpio` понимает
  `gpio-keys`/`gpios`/`linux,code`/`label` (`drivers/button/button-gpio.c:58-66,119-120`)
  и нужен только для диагностики (`button list`, `button summary`). В ядре для этих
  кнопок планируется опросный драйвер — см. `docs/buttons.md`.
* **20V усилитель (GPIOX_9), BT reset (GPIOX_17) намеренно не описаны**: U-Boot не должен
  дёргать силовые/рессетные линии. Если понадобится — в файле есть закомментированный
  `gpio-hog`-пример.
* Ethernet в U-Boot не включён: вендорский DTS содержит `internal_phy = <1>`
  (`…/sm1_s905x3_yandex.dts:369-383`), но наличие PHY на плате не подтверждено;
  для U-Boot сеть не нужна.

## 5. Как это соотносится с текущей Android-разметкой

Факты по вендорскому дереву:

* Разметка: `bootloader` 4 MiB, `reserved` 64 MiB (`AtriOS-linux/device/yandex/common/common_partition.xml`);
  A/B-слоты (`device/yandex/station_max/fstab.ab.amlogic`, `slotselect`),
  `boot` 16 MiB, `system` ext4 ≈1.5 GiB (`device/yandex/station_max/BoardConfig.mk:107-121`).
* BootROM (SM1) ищет FIP **по смещению 512 байт (сектор 1)** от начала eMMC/SD;
  при неудаче — `boot0`, затем `boot1` (`doc/board/amlogic/boot-flow.rst`, раздел «Boot Modes»).
  POC-пины для SM1 — `BOOT_4/5/6` (там же).
* Вендорские артефакты (`device/yandex/station_max/upgrade/`):
  `bootloader.img` (1 453 936 B) и `u-boot.bin.sd.bin` (1 454 448 B) — **sd.bin = 512-байтовая
  шапка + ровно тот же payload**; `u-boot.bin` (1 613 168 B) — другой образ (не совпадает побайтово с `bootloader.img`;
вероятно иная компрессия BL33 — надо проверить).
  Запись вендорским апдейтерам соответствует `bootloader.PARTITION`.

Практические выводы:

1. Подписанный FIP пишется так же, как для odroid-c4:
   `dd if=u-boot.bin.sd.bin of=$DEV conv=fsync,notrunc bs=512 skip=1 seek=1`
   (`doc/board/amlogic/pre-generated-fip.rst`).
2. **Перед перезаписью снять дамп первых секторов** и убедиться, где реально лежит
   вендорский FIP (`@AML` = `40 41 4d 4c`). Это единственный надёжный способ не
   превратить устройство в кирпич: расхождение «смещение 0 vs 512» в вендорском
   образе пока не проверено.
3. `CONFIG_ENV_IS_NOWHERE` (см. §1) означает: `saveenv` не работает, каждый старт — с
   дефолтным окружением. Для постоянного env нужен `CONFIG_ENV_IS_IN_MMC` с явным
   смещением **вне** Android-разметки — пока сознательно не включено.
4. Rootfs сейчас `system` (ext4) — под bootstd достаточно положить
   `/extlinux/extlinux.conf` + `/boot/Image` (+ `meson-sm1-yandex-station-max.dtb`) в
   этот раздел, тогда `bootflow scan` найдёт загрузку без изменения разметки.
   Вендорский `boot.img` — Android boot image; чтобы грузить его, нужен
   `BOOTMETH_ANDROID` (`boot/Kconfig:592`, тянет `ANDROID_BOOT_IMAGE`/`CMD_ABOOTIMG`).

## 6. Как проверялся DTS без полного дерева

Полное дерево U-Boot скачать не удалось (обрыв загрузки), поэтому проверка сделана
локально: из `dts/upstream/src/arm64/amlogic/` взяты `meson-sm1.dtsi`,
`meson-g12-common.dtsi`, из `dts/upstream/include/` — все транзитивные `dt-bindings`
(13 итераций `cpp`), затем:

```sh
cpp -nostdinc -undef -x assembler-with-cpp -I inc -I amlogic meson-sm1-yandex-station-max.dts -o pre.dts
dtc -I dts -O dtb -o ysm.dtb pre.dts     # exit 0
```

Результат: ошибок нет, предупреждения только из `meson-g12-common.dtsi`
(`unit_address_vs_reg`/`simple_bus_reg` для pinctrl-узлов — они есть и у апстримных
бордов). Из собранного blobs подтверждено: `mmc0 → mmc@ffe07000`, `mmc1 → mmc@ffe05000`,
`serial0 → serial@3000`, `linux,code = <0xf8>/<0x246>`, `gpios = <0x2e 0x3f 1>/<0x2e 0x40 1>`.

Что НЕ проверено: реальная сборка `make yandex-station-max_defconfig` + `make`
(нужен сетевой доступ к tarball U-Boot), линковка `board/amlogic/yandex-station-max/yandex-station-max.c`
и работа драйверов на железе.

## 7. Что проверить на железе

1. **Первым делом — USB-загрузка, без записи в eMMC**: `pyamlboot`
   (`doc/board/amlogic/boot-flow.rst`, раздел USB) умеет залить образ в RAM.
2. Консоль uart_AO (ttyS0) 115200 — должна появиться строка с `yandex-station-max`.
3. `bdinfo` → 2 GiB (иначе `/memory`/BL2 не совпали).
4. `mmc info` для `mmc 0` (eMMC, 8 бит, HS200) и `mmc 1` (SD). При CRC-ошибках:
   снизить `max-frequency` до 52 МГц и сравнить с вариантом без `meson-sm1-mmc`.
5. `button list` / `button summary` — `micmute` (GPIOA_14) и `function` (GPIOA_15).
6. `gpio status -a`: проверить, что GPIOX_9 (20V_AMPL) и GPIOX_17 (BT reset) не тронуты.
7. Замер 20 В рейла и поведение BT при старте (рессет не дёргаем — риск «залипания» BT).
8. `bootflow scan -l` на разделе `system`; проверить чтение ext4.
9. Дамп первых секторов до/после прошивки FIP (см. §5.2), плюс проверить, что `boot0` не тронут.
10. `usb start` / `ums 0 mmc 0` — аварийный доступ к eMMC.

## 8. Риски и открытые вопросы

* **DDR-инициализация.** Готовый FIP от odroid-c4 использовать нельзя: его ACS/BL2
  настроены под 4 GiB DDR4, а у нас 2 GiB LPDDR4. Вендорский BL2 собран под наш борд:
  `bootloader/uboot-repo/bl33/board/amlogic/yandex_station_v1/firmware/timing.c`
  (активные записи `__ddr_setting[]` — LPDDR4, `DDR_RFC_TYPE_LPDDR4_8Gbx1`, 1392 МГц,
  `RANK01_CH01` и `RANK0_CH01`, строки 370-516; выбор по `board_id`), плюс `fip/g12a/*.fw`.
  Безопасный путь: взять текущий вендорский FIP и заменить в нём только BL33 на наш
  `u-boot.bin`, переподписав (gxlimg поддерживает SM1 —
  `doc/board/amlogic/pre-generated-fip.rst`, раздел про gxlimg). Это надо проверить.
* **Нет SM1-специфичного pinctrl-драйвера** в U-Boot: используются g12a-таблицы.
  Для нужных нам групп (emmc/sdcard_c/uart) это то же, что у odroid-c4, но при
  проблемах с SDIO/BT это первое, что смотреть.
* **Размер FIP.** Раздел `bootloader` — 4 MiB; вендорский образ ~1.6 MB, наш (lz4)
  должен уложиться, но проверять после подписи.
* **Расхождение в ядерном DTS** (не моя зона, зафиксировать): `function` описан как
  `linux,code = <582>` с комментарием «BTN_TRIGGER_HAPPY7», но 582 = `KEY_VOICECOMMAND`
  (вендорский `key_code` 0x246). В U-Boot-DTS использован `KEY_VOICECOMMAND`.
* `sd_emmc_b` без `cd-gpios` (так и в ядерном DTS): U-Boot будет считать карту
  присутствующей; если это мешает — добавить `broken-cd`/`cd-gpios` после проверки схемы.
* Отсутствие `u-boot.bin.sd.bin` в артефактах сборки U-Boot — не ошибка: файл делает
  подписывающий тул (см. §3).
