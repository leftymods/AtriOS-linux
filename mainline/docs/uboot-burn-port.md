# Порт Amlogic USB Burning Tool в mainline U-Boot

Статус: **реализовано, собирается** (`u-boot.bin`, v2026.10, `mainline/scripts/build-uboot.sh`).
На железе не проверялось — см. §8 и §10.
Добавлено после первого отчёта: `0x33 UPLOAD` (чтение памяти/разделов,
secure-check), `Continue:32` для медленной записи и разбор вендорской
Amlogic-таблицы разделов (без env-костылей).
Протокол: `mainline/docs/uboot-burn.md`. Ветка: `mainline`. Порт: SM1 / S905X3,
Yandex Station Max.

---

## 1. Выбранная стратегия

Взят **вариант B** из постановки: протокол реализован на mainline gadget framework,
логика команд/буфера/сверок перенесена из вендорского `v2_burning`, но **без**
вендорского UDC.

Почему не A (портировать `dwc_pcd.c` + `usb_pcd.c` целиком):

* `dwc_pcd.c`/`platform.c` тянут `secure_apb.h`, `bl31_apis.h`, вендорские
  `set_usb_phy_config()`/`set_usb_boot_function()` (SMC `0x82000043`),
  `close_usb_phy_clock()`, вендорский таймер и `store_*` — то есть ровно те
  зависимости, которых в mainline нет и которые завязаны на вендорский BL31.
* Вендорский UDC — второй DWC2-драйвер в дереве; он конфликтует с
  `drivers/usb/gadget/dwc2_udc_otg.c` (глобальные `reg`, IRQ, `usb_gadget_*`)
  и с mainline USB-host.
* `optimus_core.c` (бесконечный `usb_pcd_irq()` + SOF-таймауты) заменяется
  обычным циклом `dm_usb_gadget_handle_interrupts()`.

При этом **вся протокольная часть** (`usb_pcd`-диспетчер vendor-запросов,
формат `CmdData_WriteMedia`, `add_sum`, правила 64 КиБ, текстовые команды,
ответы `OK!!` / `success` / `failed:`) перенесена из вендора практически
дословно; выброшены только `run_command()`-fallback, sdc/nand/spi/key,
AMLC/BootROM. Upload (`0x33`) поддержан для памяти и разделов `store`.

Транспорт: `dwc2_udc_otg` + свой `struct usb_gadget_driver`
(`bind`/`setup`/`disconnect`/`unbind`, EP0 + два bulk EP). Дескрипторы
отдаются свои, поэтому `bcdUSB`/`bcdDevice`/отсутствие строк — точно как в
вендоре, без `g_dnl`/composite.

---

## 2. Файлы

Канонические исходники (то, что правится):

| Файл | Назначение |
|---|---|
| `mainline/u-boot/burn/aml_burn.c` | gadget driver: дескрипторы, EP0, vendor-запросы, bulk-обмен, диспетчер команд |
| `mainline/u-boot/burn/aml_burn_storage.c` | запись/чтение разделов (`blk_dwrite`/`blk_dread`), sha1, sparse-декодер |
| `mainline/u-boot/burn/aml_burn_proto.h` | константы протокола, `struct aml_burn_img`, sparse-состояние |
| `mainline/u-boot/burn/aml_burn.h` | публичный API `aml_usb_burn_run()` |
| `mainline/u-boot/burn/Makefile` | объекты |

Интеграция в дерево U-Boot (`mainline/patches/uboot/0001-aml-usb-burn.patch`):

```
mainline/u-boot/burn/aml_burn.c          -> drivers/usb/gadget/aml_burn/aml_burn.c
mainline/u-boot/burn/aml_burn_storage.c  -> drivers/usb/gadget/aml_burn/aml_burn_storage.c
mainline/u-boot/burn/aml_burn_proto.h    -> drivers/usb/gadget/aml_burn/aml_burn_proto.h
mainline/u-boot/burn/Makefile            -> drivers/usb/gadget/aml_burn/Makefile
mainline/u-boot/burn/aml_burn.h          -> include/aml_burn.h
+ drivers/usb/gadget/Kconfig: config AML_USB_BURN
+ drivers/usb/gadget/Makefile: obj-$(CONFIG_AML_USB_BURN) += aml_burn/
```

`scripts/apply.sh` копирует `mainline/u-boot/board/**`, `*_defconfig` и `.dts`
сам, а драйвер добавляет этим патчем (`apply.sh` применяет
`mainline/patches/uboot/*.patch`), так что отдельного шага в скриптах нет.

Правки в board-файлах (копируются `apply.sh`, не патчем):

* `mainline/u-boot/yandex-station-max_defconfig`: `CONFIG_AML_USB_BURN=y`;
* `mainline/u-boot/board/amlogic/yandex-station-max/yandex-station-max.c`:
  хук `meson_board_late_init()` (его вызывает `arch/arm/mach-meson/board-common.c`
  из `board_late_init()`), вход по env `usb_burning`;
* `mainline/u-boot/meson-sm1-yandex-station-max.dts`: `&usb { status = "okay";
  dr_mode = "peripheral"; }` и `&dwc2 { status = "okay"; }`. Glue-драйвер
  `dwc3-meson-g12a` в `child_pre_probe` сам переводит OTG-PHY (`usb2_phy1`)
  в peripheral, когда пробрасывается DWC2-ребёнок. DWC3-хост (`usb@ff500000`)
  оставлен выключенным.

Пересобрать патч после правки `mainline/u-boot/burn/`:

```sh
# 1) положить файлы в дерево (см. таблицу соответствия выше)
# 2) в чистом git-дереве с девственным drivers/usb/gadget/{Kconfig,Makefile}:
git add -A && git diff --cached > mainline/patches/uboot/0001-aml-usb-burn.patch
```

Проще: держать `mainline/u-boot/burn/` как источник истины и обновлять патч
тем же способом, каким он был создан (временный git-репозиторий с базовыми
`Kconfig`/`Makefile` + скопированные файлы).

---

## 3. Сборка и Kconfig

```
CONFIG_AML_USB_BURN=y            # bool, depends on USB_GADGET && DM_USB_GADGET && PARTITIONS
                                 # select SHA1
CONFIG_MESON_G12A_USB_PHY=y      # amlogic,g12a-usb2-phy (usb2_phy0/1), иначе glue-драйвер
                                 # не сможет перевести OTG-PHY в peripheral и DWC2 не probe'ится
```

Собирается объектами `drivers/usb/gadget/aml_burn/`; UDC —
`CONFIG_USB_GADGET_DWC2_OTG` (уже был включён), `CONFIG_DM_USB_GADGET`,
`CONFIG_USB_GADGET_DWC2_OTG_PHY_BUS_WIDTH_8`. `CONFIG_USB_GADGET_DOWNLOAD`
(g_dnl/ums) остаётся для команды `ums`, с burn-драйвером не конфликтует.

Проверено:

```
$ mainline/scripts/build-uboot.sh -j 6
...
  CC      drivers/phy/meson-g12a-usb2.o
  CC      drivers/usb/gadget/aml_burn/aml_burn.o
  CC      drivers/usb/gadget/aml_burn/aml_burn_storage.o
...
        837296  mainline/out/uboot/u-boot.bin
$ nm mainline/out/uboot/build/u-boot | grep -i aml
... T aml_burn_add_sum, T aml_store_init, T aml_store_select, T aml_img_begin,
    T aml_img_write, T aml_img_verify_sha1, T aml_up_begin, T aml_up_read,
    t do_aml_burn, D _u_boot_list_2_cmd_2_aml_burn, T meson_board_late_init
```

---

## 4. USB

| Параметр | Значение |
|---|---|
| VID:PID | `1b8e:c003` (жёстко, как в вендоре; `CONFIG_USB_GADGET_*_NUM` не используются) |
| bcdUSB / bcdDevice | `0x0200` / `0x0007` |
| bDeviceClass | 0 (`per-interface`) |
| bMaxPacketSize0 | 64 |
| Строки | индексы 0 (=строк нет); строки 1..3 отдаются на случай запроса хостом |
| Конфигурация | 1 интерфейс, `bConfigurationValue=1`, `bmAttributes=0xc0`, `MaxPower=1` |
| Интерфейс | class `0xff`, subclass/protocol 0, 2 EP |
| EP | `0x81` IN bulk, `0x02` OUT bulk, 512 Б (HS) / 64 Б (FS) |

EP0-запросы: `GET_DESCRIPTOR` (device/config/string), `SET_CONFIGURATION`,
`GET_CONFIGURATION`, `SET_INTERFACE`, `GET_INTERFACE`, `GET_STATUS`,
`CLEAR/SET_FEATURE`. `DEVICE_QUALIFIER` — stall, как в вендоре (устройство
только HS/FS, без SS).

`SET_CONFIGURATION` каждый раз заново включает EP: UDC при bus-reset
перезапускает ядро DWC2, но `usb_ep.enabled` остаётся взведённым, поэтому
`usb_ep_enable()` без предварительного `usb_ep_disable()` был бы no-op
(это же делает composite).

---

## 5. Поддержанные запросы и команды

### 5.1 Vendor-запросы

| Код | Поддержка | Поведение |
|---|---|---|
| `0x20` identify | да | 8 байт `00 07 00 10 00 00 00 00` (`stage_minor=16`, TPL) |
| `0x30` TPL_CMD | да | EP0 OUT, строка; выполняется при `wIndex==1`; ответ — через `0x31` |
| `0x31` TPL_STAT | да | EP0 IN, 512 байт результата |
| `0x32` DOWNLOAD | да | EP0 OUT 16/32 байта, затем bulk OUT, ответ bulk IN `ackLen` (512) |
| `0x34` BULKCMD | да | EP0 OUT строка, результат bulk IN 512 |
| `0x33` UPLOAD | да | EP0 IN: 16 байт `{u32 0xefe8, u32 len, 0, 0}`, затем bulk IN `len` байт (≤ 64 КиБ), см. §5.4 |
| `0x01/0x02/0x03/0x04/0x05/0x06/0x07` | нет | доступ к произвольной памяти/регистрам (BootROM-путь) — stall |
| `0x11/0x12` LARGE_MEM | нет | альтернативный путь в ОЗУ — stall |

### 5.2 Текстовые команды (общий диспетчер `aml_cmd_exec`)

| Команда | Результат / поведение |
|---|---|
| `low_power` | `success` (в U-Boot нет дисплея/лимита тока, no-op) |
| `disk_initial <n>` | `success` — `blk_get_device_by_str()` + `part_init()` (erase не делается, аргумент игнорируется) |
| `bootloader_is_old` | **всегда `failed:bootloader is new`** — tool считает загрузчик новым и не стирает `boot` |
| `download <media> <part> <type> <size>` | подготовка приёма; `media` только `store`; `type`: `normal`, `sparse`, `bootloader`, `dtb`; `part` — имя раздела |
| `download get_status` | `success`, когда `wire == size` (образ принят), иначе `failed:not complete` |
| `download is_ready` | `success`, когда команда `download` получена и образ ещё не принят |
| `verify sha1sum <40 hex>` | sha1 раздела (`size` байт с начала раздела), сверка с hex; `failed:...` при несовпадении. Для `sparse` — `failed` (см. §9) |
| `save_setting` | `success` (env в mainline не персистентный) |
| `burn_complete <n>` | `success`, затем действие: `0x0/0x2` — poweroff, `0x1/0xdc/0xeb` — reset, `0x3` — остаться в режиме (ждать выдёргивания кабеля), `0xe1` — только ответ |
| `reset` | `success`, затем reset |
| `poweroff` | `success`, затем poweroff (если есть `SYSRESET`) |
| `echo ...` | `success` (используется для выравнивания 4 байт после reset) |
| `erase_bootloader` | `failed:erase_bootloader disabled` — раздел `boot` не трогаем |
| `upload mem <hex> normal <size>` | подготовка чтения памяти (адрес — hex, обязателен); данные отдаются по `0x33`, см. §5.4 |
| `upload store <part> normal <size>` | подготовка чтения раздела; `size` ≤ размера раздела; `sparse` не поддержан |
| `write_raw_img`, `sha1sum`, `simg2part`, `md`, `key`, `tempcontrol`, `get_chipid`, любые другие | `failed:unsupported command "<cmd>"` |

**`run_command()` не портирован.** Неизвестная строка не выполняется, а
отвергается — иначе USB-порт превращается в удалённый shell (вендор именно
так и работает).

### 5.3 `0x32` в деталях

`struct aml_burn_write_media` (32 байта, pack(4), как `CmdData_WriteMedia`):
`retryTimes, userDataLen, sequenceNo, dataCheckSum, dataCheckAlgorithm,
bulkInAckLen, reserv[12]`.

* `wLength == 0x20` — «новый» протокол: берём `alg` и `ackLen` из структуры;
  иначе — `ADDSUM` и 512.
* Пакеты, кроме последнего, обязаны быть кратны 64 КиБ (иначе
  `failed:short packet ...`), последний — любой длины.
* `dataCheckAlgorithm`: `0x00EE` — без проверки, `0x00EF` — add-sum
  (сумма 32-битных LE-слов, хвост 1..3 байта маскируется), `0x00F0` (CRC32) —
  `failed:checksum algorithm ... not supported`.
* Ответ на пакет — bulk IN `ackLen` (обычно 512) байт: `OK!!` (с нулевым
  хвостом) или `failed:...` с текстом как у вендора
  (`Alg[ef]0x: genSum ... != org ..., media error ...`).
* Повторный пакет (`sequenceNo < ожидаемого`) не пишется второй раз, но
  подтверждается `OK!!` — так tool переживает потерю ack.
* `Continue:32` реализован (см. §5.5): при медленной записи target отвечает
  им до записи пакета, а `OK!!` — после. Обычный путь (`OK!!` сразу,
  ресенды, `failed:...`) не изменился.

### 5.4 `0x33` UPLOAD (чтение)

Порт вендорского readback: команда `upload <media> <part> normal <size>`
(приходит как `0x34`/`0x30`), затем столько запросов `0x33`, сколько нужно.

* `0x33` — vendor-запрос **IN**, `wLength = 16`. EP0-ответ:
  `{u32 0xefe8, u32 len, u32 0, u32 0}` (little endian, как `_resultInfo` у
  вендора: `[0]=0xefe8`, `[4]=длина этого bulk-транша`).
* Сразу после EP0-стадии устройство отдаёт **bulk IN `len` байт**.
  `len = min(остаток, 64 КиБ)`; хост повторяет `0x33` для следующего куска
  (вендор тоже ограничивает транш `transferUnitSz` = 64 КиБ).
  Когда остаток равен 0, ответ — `{0xefe8, 0}` без bulk-стадии.
* `media=mem`: `part` — обязательный hex-адрес (`0xff634500`), чтение идёт
  обычным `memcpy`. Исключение — слово `0xff800228` (`Encrypt_reg` из
  `platform.conf`, бит 4 = secure boot): читать его через secure APB нельзя,
  поэтому отдаются нули, т.е. «не secure» — ровно то, что проверяет
  `pyamlboot` (`BurnStepSecureBootCheck`).
* `media=store`: `part` — имя раздела, данные читаются тем же
  `store_read()`/`blk_dread()`, что и verify; `size` не больше раздела.
* `sparse`, `nand`, `sdmmc`, `spiflash`, `key`, `dtb`-как-адрес — `failed:`.

Как это выглядит для `pyamlboot` (secure-check на TPL-стадии):

```python
d.bulkCmd('upload mem 0xff800228 normal 0x4')   # -> success
data = d.readMedia(4)                            # 0x33 + 4 байта bulk IN
```

`readMedia()` в pyamlboot читает весь `size` одним запросом, поэтому
большие объёмы (больше 64 КиБ) им не читаются — для Windows-tool это не
проблема, он запрашивает по траншу.

### 5.5 `Continue:32`

Вендор шлёт `Continue:32` из фонового таймера, пока идёт write-back
(«не чаще 16 с и после 8 МиБ»). Здесь запись синхронная, поэтому `Continue:32`
ставится в очередь bulk IN **перед** пакетом, который ожидается медленным, а
`OK!!` — после записи; у IN-эндпоинта два отдельных `usb_request`, UDC
отправляет их подряд. Хост читает два ответа, как и от вендора
(`pyamlboot` после `Continue:32` спит 3 с и читает снова, до 10 с).
Если `Continue:32` не успел уйти (ошибка очереди), просто пишем и отвечаем
`OK!!`, как обычно.

Условия (константы в `aml_burn.c`):

| Константа | Значение | Смысл |
|---|---|---|
| `AML_BURN_CONTINUE_SLOW_MS` | 1000 мс | предыдущая запись пакета была дольше — считаем носитель медленным |
| `AML_BURN_CONTINUE_MIN_MS` | 1500 мс | не чаще одного раза в 1.5 с |
| `AML_BURN_CONTINUE_MIN_BYTES` | 8 МиБ | не чаще одного раза на 8 МиБ принятых данных (первый `Continue` в «окне» разрешён сразу) |
| `AML_BURN_CONTINUE_URGENT_MS` | 6000 мс | если один пакет писался дольше — лимит по 8 МиБ игнорируется (иначе tool отвалится по таймауту) |

Счётчики сбрасываются на каждой команде `download`. Никакого «отложенного»
`OK!!` нет: запись и ответ выполняются сразу после постановки `Continue:32`
в очередь, поэтому пайплайнинг пакетов со стороны хоста ничего не ломает.

---

## 6. Запись разделов

`aml_burn_storage.c`:

* устройство: env `aml_burn_ifname` / `aml_burn_dev` (по умолчанию `mmc 0`),
  инициализация — `blk_get_device_by_str()` (внутри делает `part_init()`);
* раздел ищется по имени из команды tool, порядок поиска:
  1. `part_get_info_by_name(desc, part, &info)` — GPT/MBR (если разметка
     конвертирована);
  2. вендорская Amlogic-таблица «MPT» (§6.1) — заводская разметка eMMC;
  3. env-костыль `aml_burn_part_<name>=<start>:<size>` (в блоках
     `blk_desc->blksz`), последний шанс для нестандартной разметки:

     ```
     aml_burn_part_boot=0x800:0x20000        # start:size в блоках
     aml_burn_part_system=0x20800:0x2e0000
     ```

     Имена с префиксом `aml_burn_part_` + имя раздела как его пришлёт tool
     (`boot`, `system`, `_aml_dtb`, `bootloader`, `logo`, ...);
* запись — `blk_dwrite()` с разбивкой на блоки и bounce-буфером для
  невыровненных хвостов; чтение для verify — `blk_dread()`;
* `type=sparse` — потоковый разбор AOSP sparse (`RAW`/`FILL`/`DONT_CARE`/
  `CRC32`), образ не буферизуется целиком;
* `part=bootloader` — пишется как есть в начало раздела `bootloader`
  (FIP BL2+BL33). Ограничение вендора 2 МиБ не перенесено: проверяется только
  размер раздела.
* sha1 для verify считается потоково (`sha1_starts/update/finish`,
  `CONFIG_SHA1`) по содержимому раздела.

### 6.1 Вендорская таблица разделов («MPT»)

Заводская разметка Amlogic — не GPT/MBR: таблица лежит в начале
inherent-раздела `reserved`, т.е. по байтовому смещению **36 МиБ**
(`bootloader` 4 МиБ @ 0 + 32 МиБ зазор, см. `emmc_partitions.h`:
`MMC_BOOT_DEVICE_SIZE`/`MMC_BOOT_PARTITION_RESERVED`). Формат — как
`struct ptbl_rsv` в `drivers/mmc/aml_emmc_partition.c`, структуры
продублированы в `aml_burn_proto.h` (`struct aml_ptbl`):

```
offset 0   u8  magic[4]      "MPT\0"
offset 4   u8  version[12]   "01.00.00" (или "01.02.00")
offset 16  u32 count         1..32
offset 20  u32 checksum
offset 24  32 × 40 байт: {u8 name[16]; u64 size; u64 offset; u32 mask; u32 pad}
```

Итого `sizeof(struct aml_ptbl)` = 1304 байта; читаем `DIV_ROUND_UP(1304, blksz)`
блоков (3 × 512 Б) в выровненный буфер на 4 КиБ.

* **Смещения не берутся из таблицы**, а считаются как у вендора
  (`_calculate_offset()`): `bootloader` @ 0, зазор 32 МиБ после него, дальше
  зазор 8 МиБ между разделами; `size == ~0ull` — «весь остаток диска»
  (последний раздел, обычно `data`).
* **Checksum** повторяет вендорский баг версии 1 (`_calc_iptbl_check_v1()`:
  первая запись суммируется `count` раз) — иначе реальные таблицы не
  проходят проверку. Версия 2 считается по всем записям.
* Несовпадение magic/версии/count/checksum — таблица игнорируется, дальше
  пробуется env (в консоли будет строка с причиной).
* Таблица кэшируется на устройство; `disk_initial` (`aml_store_exit()`)
  сбрасывает кэш и перечитывает её.

На прошивке Station Max таблица содержит 21 запись: inherent
`bootloader`/`reserved`/`cache`/`env` плюс 18 записей из dtb
(`logo … data`, `cache` перекрывает inherent-запись). Раскладка, посчитанная
парсером для этой таблицы: `bootloader` 0x0, `reserved` 0x2400000,
`cache` 0x6c00000, `env` 0x4d400000, `boot` 0x59600000,
`system` 0x86600000, `data` — остаток диска.

---

## 7. Как включить

1. Собрать и прошить U-Boot (FIP собирается отдельно, как обычно).
2. Войти в режим:
   * из консоли U-Boot: `aml_burn` (5 с на подключение) или
     `aml_burn 1000 750` (1 с на подключение, 750 мс на identify);
   * автоматически при загрузке:

     ```
     setenv usb_burning 1
     setenv aml_burn_timeout 1000      # мс на подключение хоста (по умолчанию 5000)
     setenv aml_burn_identify 750      # мс на 0x20 после SET_CONFIGURATION (0 = бесконечно)
     ```

     Хук вызывается из `meson_board_late_init()` (т.е. из `board_late_init`),
     после инициализации MMC. Env не сохраняется (`CONFIG_ENV_IS_NOWHERE`),
     поэтому после прошивки `usb_burning` надо снять в своей среде загрузки.
3. На хосте — Amlogic USB Burning Tool (Windows) или `pyamlboot`, образ
   `yandex_s905x3_custom_v130.img` и т.п.

**Sticky-регистр не используется.** Вендор на `board_init`/`board_late_init`
проверяет `readl(P_PREG_STICKY_REG2)` (`0xff6345c8`) на `0x1b8ec003`
(`VID<<16|PID`) и `0x1b8ec004`. Кто и когда пишет это значение — не найдено
ни в BSP, ни на трассе; на этой плате поведение не проверялось. Поэтому
авто-вход по регистру **описан, но не включён**. Если понадобится:

```c
/* только после проверки на железе, что регистр действительно пишется */
if (readl(0xff6345c8) == 0x1b8ec003)
        aml_usb_burn_run(5000, 0);
```

`0xff6345c8` = `P_PREG_STICKY_REG2` = `0xff634400 + (0x72 << 2)`.

---

## 8. Проверка на железе

### 8.1 Что должно быть видно на Linux-хосте

```sh
lsusb -v -d 1b8e:c003
# bcdUSB 2.00, idVendor 0x1b8e, idProduct 0xc003, bcdDevice 0x0007
# bMaxPacketSize0 64, bConfigurationValue 1, bmAttributes 0xc0, MaxPower 1
# Interface 0: bInterfaceClass 0xff, bNumEndpoints 2
# EP 0x81 Bulk 512, EP 0x02 Bulk 512

dmesg | tail
# "new high-speed USB device number N using xhci_hcd"
# "idVendor=1b8e, idProduct=c003", скорость 480 Mbps
```

Строк (`iManufacturer`/`iProduct`) нет — это правильно, как в вендоре.

### 8.2 Трасса протокола

```sh
sudo modprobe usbmon
# usbmon + Wireshark/USBPcap на Windows:
#  - первый vendor-запрос после SET_CONFIGURATION — 0x20, ответ 8 байт, [3]=16
#  - далее 0x34/0x30, затем 0x32 + bulk OUT 0x10000 + bulk IN 0x200 ("OK!!")
```

В консоли U-Boot при этом печатается:

```
aml_burn: waiting for Amlogic USB Burning Tool (VID:PID 1b8e:c003, 5000 ms)
aml_burn: configured (high speed, EP 0x81/0x02)
aml_burn: ID[16]
aml_burn: tplcmd[    low_power]
aml_burn: bulkcmd[disk_initial 0]
aml_burn: download store boot normal 0x9e2000
aml_burn: partition boot: start 0x..., ... blocks of 512 B
aml_burn: boot complete (0x9e2000 bytes)
aml_burn: verify ok for boot (<sha1>)
aml_burn: burn complete, rebooting
```

### 8.3 Быстрый тест без Windows

`pyamlboot` (тот же протокол, `stage_minor==16`) — минимальный прогон:

```python
# pip install pyamlboot; устройство уже в aml_burn
from pyamlboot import pyamlboot
d = pyamlboot.UsbDevice.from_device(pyamlboot.find_device())
print(d.socid())          # stage_minor должен быть 16
print(d.tplcmd("disk_initial 0"))

# 0x33: secure-check (ровно то, что делает BurnStepSecureBootCheck)
print(d.bulkCmd("upload mem 0xff800228 normal 0x4"))   # -> success
print(d.readMedia(4))                                  # 00 00 00 00 = не secure

# 0x33: чтение начала раздела boot (должен совпасть с образом)
print(d.bulkCmd("upload store boot normal 0x1000"))
print(d.readMedia(0x1000))
```

В консоли U-Boot при этом:

```
aml_burn: upload mem/0xff800228 type normal size 0x4
aml_burn: upload chunk 0x4 at 0x0
aml_burn: upload complete (0x4 bytes)
aml_burn: partition boot from Amlogic table: 0x59600000+0x1000000
```

Проверка разметки без записи: `aml_burn`, затем из другого окна
`pyamlboot` (или tool) — в консоли должна появиться строка
`aml_burn: Amlogic partition table v1, 21 partitions`, а `download store boot
normal 0x100` (с последующим `download get_status`) пройти без env-костылей.

Тестовые образы (по возрастанию сложности):

| # | Образ | Что проверяет |
|---|---|---|
| 1 | самодельный пакет: `PARTITION/boot` (normal) + `VERIFY/boot` | download/get_status/verify |
| 2 | пакет: `bootloader` + `_aml_dtb` + `boot` + `VERIFY/*` | bootloader-путь, dtb, sha1 |
| 3 | `yandex_s905x3_custom_v130.img` | полный цикл, 64-КиБ поток, скорость |
| 4 | пакет со `sparse`-system | sparse RAW/FILL/DONT_CARE |

Критерий успеха: tool доходит до «Burning successfully»; `download get_status`
→ `success`; `verify sha1sum` совпал; после `burn_complete 1` устройство
грузится; разделы побайтово совпадают с образами (`dd` + `sha256sum`).

### 8.4 Что померить

* скорость: ожидание ≥ 20 МБ/с на HS bulk; у нас один `usb_ep_queue` на 64 КиБ
  (внутри UDC — пакеты по 512 Б), без нарезки на 4 КиБ как в вендоре;
* таймауты tool’а между пакетами (< 10 с);
* поведение при повторной отправке пакета (оборвать кабель/исказить сумму —
  target должен вернуть `failed:... genSum ...` и принять ресенд);
* `Continue:32`: на медленном носителе (или искусственно замедленной записи)
  в usbmon должно быть видно `Continue:32` перед `OK!!`, не чаще 1.5 с и не
  чаще 8 МиБ; tool при этом не отваливается по таймауту.

---

## 9. Чего нет / ограничения

| Что | Статус |
|---|---|
| BootROM-путь: AMLC (`0x50/0x60`), `0x01/0x02/0x05`, пароль `0x35` | не нужен: `0x20` отдаёт stage 16, tool идёт TPL-веткой |
| `0x33 UPLOAD` | есть для `mem` (hex-адрес) и `store` (раздел); `sparse`/`nand`/`key`/`dtb`-адрес — `failed` |
| `readback` в Windows-tool, `md` | `md` (текстовая команда) нет; readback идёт через `0x33` |
| `sha1sum <part> <len> <hex>` (sdc-ветка) | нет |
| sparse + `verify sha1sum` | запись sparse работает, verify — `failed` (нужен chunk-лог, чтобы пересчитать sha1 по разделам; в v130 `system` — `normal`, так что полный цикл не страдает) |
| `mem`-media (`download mem dtb ...`) | нет (только `store`) |
| `nand`/`sdmmc`/`spiflash`/`key`/`ubifs` | нет |
| `erase_bootloader` | отключено (не стираем `boot`) |
| `Continue:32` | есть (см. §5.5) |
| `Continue:34` (долгий `0x34`, например `verify`) | нет: ответ приходит после выполнения команды; `pyamlboot` ждёт его 150 с, у Windows-tool таймаут не проверен |
| Авто-вход по sticky-регистру | описан, не включён (так и оставлено) |
| Заводская Amlogic-разметка eMMC | парсится «MPT»-таблица (§6.1); при несовпадении magic/checksum — env `aml_burn_part_<name>` |
| Чтение secure APB (`0xff800228`) | отдаём нули; прочие адреса читаются `memcpy` как есть — на регистрах/незамапленном может быть abort |

---

## 10. Риски

| Риск | Почему / что делать |
|---|---|
| Разметка eMMC | «MPT»-таблица читается с 36 МиБ и проверяется по magic/version/count/checksum. Если на конкретном экземпляре смещение/формат другие, `download` вернёт `failed:partition not found` — это безопасно, но прошивка не пойдёт: тогда задать `aml_burn_part_*` и посмотреть в консоли причину отказа парсера. `mmc part` для заводской разметки пуст — это нормально. |
| Смещения из таблицы | вендор их не хранит, а считает (32 МиБ после `bootloader`, 8 МиБ между разделами). Если на устройстве зазор другой, запишем не туда — перед первым прожигом сверить `partition boot from Amlogic table: 0x…` с дампом (например, с `_aml_dtb` из пакета/`sm1.dtb`). |
| Раздел `bootloader` | пишется FIP как есть. Ошибка в образе/смещении = кирпич (восстановление только через ROM USB / SD). Перед первым прожигом снять дамп раздела и убедиться, что `bootloader_is_old` реально приводит к пропуску стирания. |
| Secure boot | если в efuse включена проверка подписи, неподписанный BL2/BL33 не загрузится; gadget при этом отработает штатно. Проверять на устройстве с известным состоянием efuse. |
| Дескрипторы | tool и pyamlboot ищут ровно `1b8e:c003`; проверять `lsusb -v` (bcdUSB/EP/класс) до первого прожига. |
| Вендорский tool | Windows-бинарник не анализировался; `wValue/wIndex` для `0x32` не проверялись трассой. Если tool шлёт старый вариант (без `alg/ackLen`), код это поддерживает, но подтвердить usbmon’ом. |
| Скорость | неизвестна; `Continue:32` включается сам, когда пакет пишется дольше 1 с (см. §5.5). Если tool всё равно рвёт связь — поднять `AML_BURN_CONTINUE_SLOW_MS`/`URGENT_MS` или нарезать запись мельче. |
| `0x33` и secure APB | читается `memcpy` по физическому адресу: для `0xff800228` отдаём нули, но чтение других secure/незамапленных адресов может дать data abort. Не читать регистры этим путём без проверки карты памяти. |
| Env | `save_setting` — no-op; вендорская логика `upgrade_step`/`firstboot` не воспроизводится, ОС после прошивки может вести себя иначе (wipe data). |
| Пиковое ОЗУ | один буфер 64 КиБ + 2×512 Б; sparse пишется потоково, 1.5 ГиБ образ в память не кладётся. |
| Цепочка probe USB | gadget = `dwc3-meson-g12a` (glue, `&usb`) → `child_pre_probe` force peripheral → `dwc2` → `dwc2_udc_otg`. Если `CONFIG_MESON_G12A_USB_PHY` выключить или `&usb`/`&dwc2` в DTS не `okay`, `uclass_first_device_err(UCLASS_USB_GADGET_GENERIC)` вернёт ошибку; в консоли будет `aml_burn: no USB device controller`. |
| `meson_board_late_init` | weak-хук в `arch/arm/mach-meson/board-common.c`; если в дереве появится свой `board_late_init` для платы — конфликт символов (проверено: у этой платы его нет). |
