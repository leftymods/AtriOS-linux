# Amlogic USB Burning Tool: протокол и план порта в mainline U-Boot

Статус: **восстановлено по исходникам вендорского BSP** (не по трассе USB). На железе не проверялось.
Дата: 2026-10-28. Ветка: `mainline`. Порт: SM1 / S905X3, Yandex Station Max.

## 0. Поправка к постановке задачи: файлы не потеряны

Задача формулировалась как «файлы `v2_burning.c`, `v2_burning_i.h`, `v2_sdc_burn/*` отсутствуют».
Фактически в `/home/leftymods/AtriOS/AtriOS/AtriOS-linux` это **sparse checkout**: на диске лежит
только 7 файлов каталога `v2_burning`, но в `git HEAD` есть всё (40 файлов):

```
$ git -C .../bootloader/uboot-repo ls-tree -r HEAD --name-only | grep v2_burning
.../v2_usb_tool/usb_pcd.c        <- НЕ было на диске, а это и есть транспорт протокола
.../v2_usb_tool/optimus_core.c   <- «state machine» (int v2_usbburning)
.../v2_usb_tool/optimus_transform.c
.../v2_burning_i.h
.../v2_usb_burn/*, v2_sdc_burn/*, v2_common/* (полный набор)
```

Имена `v2_burning.c` / `v2_sdc_burn/optimus_sdc_burn.c` в этой линейке BSP вообще отсутствуют —
точка входа называется `aml_v2_burning.c` + `v2_usb_tool/optimus_core.c`. Ниже всё цитируется
по версии из `git HEAD` (файлы получены `git show HEAD:<path>`, рабочее дерево не менялось).

Воспроизвести выгрузку исходников (без изменения дерева):

```sh
cd /home/leftymods/AtriOS/AtriOS/AtriOS-linux/bootloader/uboot-repo
git ls-tree -r HEAD --name-only | grep v2_burning          # 40 файлов
git show HEAD:./bl33/drivers/usb/gadget/v2_burning/v2_usb_tool/usb_pcd.c > /tmp/usb_pcd.c
```


Дополнительно найден второй, более старый (2017) экземпляр того же кода:
`/home/leftymods/UB-AtriOS/drivers/usb/gadget/v2_burning/` — он **побайтово совпадает** по
`usb_pcd.c`/`optimus_transform.c` с 2019-й версией, кроме правок стиля, `getenv_ulong()` и
удалённой команды `read_raw_img`. То есть протокол между 2017 и 2019 не менялся.

Обозначения путей ниже:
* `bl33/…` = `/home/leftymods/AtriOS/AtriOS/AtriOS-linux/bootloader/uboot-repo/bl33/…`
* «git HEAD» — файл есть в git, но не в рабочем дереве (sparse checkout).
* `v2019/…` = локальная копия из `git HEAD` (для точных номеров строк).

### Что известно достоверно, а что нет

| Уровень | Источник | Достоверность |
|---|---|---|
| Дескрипторы, коды запросов, формат команд | исходники BSP | подтверждено |
| Порядок команд при прошивке | исходники BSP + независимая реализация pyamlboot | подтверждено дважды |
| Формат пакета Amlogic v2 | исходники + разбор реального `yandex_s905x3_custom_v130.img` | подтверждено |
| Конкретные значения `wValue`/`wIndex`, которые посылает **Windows-tool** | не проверялось трассой | частично (по поведению target’а и pyamlboot) |
| Кто и когда пишет `PREG_STICKY_REG2 = 0x1b8ec003` | в дереве не найдено | неизвестно |
| Алгоритм поля `crc` в заголовке пакета | в дереве **нет реализации** `image_check()` | неизвестно |

---

## 1. Как включается burn-режим

### 1.1 Три независимых входа

**(a) ROM-загрузка с USB (заводской режим).**
`bl33/board/amlogic/yandex_station_v1/yandex_station_v1.c:608-616`:

```c
int board_init(void)
{
#ifdef CONFIG_AML_V2_FACTORY_BURN
    if ((0x1b8ec003 != readl(P_PREG_STICKY_REG2)) && (0x1b8ec004 != readl(P_PREG_STICKY_REG2))) {
                aml_try_factory_usb_burning(0, gd->bd);
    }
#endif
```

То есть **самый первый** шаг `board_init`, до инициализации USB-host, NAND, I2C, дисплея.
`0x1b8ec003` = `VID<<16 | PID` = `0x1b8e:0xc003` — то же самое, что в дескрипторе
(`v2019/usb_pcd.c:54-55`). Второе значение `0x1b8ec004` — вариант PID, в этом дереве не
встречается (в дескрипторе всегда `0xC003`), смысл — открытый вопрос.

`aml_try_factory_usb_burning()` (`bl33/drivers/usb/gadget/v2_burning/aml_v2_burning.c:150-161`):
работает **только** если `is_tpl_loaded_from_usb()`, при `flag==0` дополнительно делает
`mmc_initialize(bis)` (нужен, чтобы tool мог писать в eMMC через `store`), затем
`aml_burn_usb_producing()`.

`is_tpl_loaded_from_usb()` (`aml_v2_burning.c:62-80`):

```c
const unsigned boot_id = readl(P_AO_SEC_GP_CFG0) & 0xf;   // 5 == BOOT_DEVICE_USB
const unsigned forceUsbBoot = readl(P_AO_SEC_GP_CFG7);
return (BOOT_DEVICE_USB == boot_id) || (forceUsbBoot & (1U<<31));
```

**(b) Env-команда `update` (обновление из Linux/Android).**
`bl33/board/amlogic/configs/yandex_station_v1.h:114`:

```
"usb_burning=update 1000\0"
```

и `:236-247`:

```
"update="
   /*first usb burning, second sdc_burn, third ext-sd autoscr/recovery, last udisk autoscr/recovery*/
   "run usb_burning; "
   "run sdc_burning; "
   ...
```

`update` — это `U_BOOT_CMD` из `v2019/optimus_core.c:60-81` (`do_v2_usbtool`): `argv[1]` = timeout
ожидания enumeration, `argv[2]` = `identifyWaitTime` (0 → env очищается). `update 1000` = 1 с.

**(c) «Уже прошивали» — sticky-регистр.**
`board_late_init`, `yandex_station_v1.c:706-711`:

```c
#ifdef CONFIG_AML_V2_FACTORY_BURN
    printf("YANDEX: board_late_init, try factory burn\n");
    if (0x1b8ec003 == readl(P_PREG_STICKY_REG2))
        aml_try_factory_usb_burning(1, gd->bd);
        aml_try_factory_sdcard_burning(0, gd->bd);
#endif
```

Замечания:
* `aml_try_factory_usb_burning(1, …)` — «поздний» вход: без `mmc_initialize` (флаг 1), после
  полной инициализации платы.
* В исходнике **нет фигурных скобок** — `aml_try_factory_sdcard_burning(0, gd->bd)` вызывается
  безусловно (на каждом boot). Для порта это не нужно, но объясняет поведение вендорской прошивки.
* Кто пишет `0x1b8ec003` в sticky, в дереве не найдено (`grep` по всему `AtriOS-linux`).
  Гипотеза: это делает сам PC-tool через vendor-request `0x01 WRITE_MEM` (регистр
  `PREG_STICKY_REG2 = 0xff634400 + (0x72<<2)`, см. `bl33/arch/arm/include/asm/arch-g12b/secure_apb.h:733`),
  чтобы при следующем включении U-Boot снова поднял burn-режим. **Не подтверждено.**

### 1.2 Что происходит до инициализации USB

`aml_burn_usb_producing()` (`aml_v2_burning.c:115-124`):

```c
set_usb_boot_function(CLEAR_USB_BOOT);                  // SMC, см. ниже
optimus_work_mode_set(OPTIMUS_WORK_MODE_USB_PRODUCE);   // 0xefe6, optimus_download.h:189
close_usb_phy_clock(0);                                 // «disconnect before re-connect to enhance pc compatibility»
return v2_usbburning(20000);                            // 20 000 мс
```

`set_usb_boot_function(x)` — это **SMC в BL31**, не SCPI:
`bl33/arch/arm/cpu/armv8/g12a/bl31_apis.c:337-347`:

```c
register long x0 asm("x0") = SET_USB_BOOT_FUNC;   // 0x82000043
register long x1 asm("x1") = command;             // CLEAR_USB_BOOT == 1
asm volatile("smc #0" ...);
```

Константы: `bl33/arch/arm/include/asm/arch-g12a/bl31_apis.h:73-77`
(`SET_USB_BOOT_FUNC 0x82000043`, `CLEAR_USB_BOOT 1`, `FORCE_USB_BOOT 2`, `RUN_COMD_USB_BOOT 3`).

`v2_usbburning()` (`v2019/optimus_core.c:32-58`): `set_usb_phy_config(EXT_CLOCK)`;
`usb_parameter_init(timeout)`; `usb_pcd_init()` (сброс PHY/ядра DWC2 через
`P_RESET1_REGISTER_USB` — `bl33/drivers/usb/gadget/v2_usb_tool/platform.c:95-136`); далее вечный
цикл `usb_pcd_irq()` до ненулевого кода возврата (выход = «PC не пришёл»).

`usb_parameter_init()` (`v2019/usb_pcd.c:252-272`): `time_out_val = timeout ? timeout :
USB_ROM_CONN_TIMEOUT` (5 000 000 → 5 с, `platform.h:121`), `optimus_buf_manager_init(16*1024)`,
`optimus_download_init()`.

Таймауты (`v2019/usb_pcd.c:275-...`):
* нет SOF дольше `time_out_val/2` → печать `noSof`, выключение PHY, возврат;
* прошло больше `time_out_val` → «Try connect time out» и выход;
* если в env задан `identifyWaitTime` (см. `optimus_download.h:205`) и включён режим
  `OPTIMUS_WORK_MODE_USB_UPDATE`, то с момента `SET_CONFIGURATION` (`v2019/usb_pcd.c:430-436`)
  отсчитывается окно, за которое tool обязан прислать `AM_REQ_IDENTIFY_HOST`; иначе выход.
  Именно это даёт `try_auto_burn=update 700 750` (`yandex_station_v1.h:117`): 700 мс на
  подключение, 750 мс на identify — если Burning Tool не запущен, U-Boot идёт грузить систему.

### 1.3 Итоговая схема

```
ROM (boot_id==5) --BL2--> U-Boot board_init --> aml_try_factory_usb_burning(0) --> USB gadget (VID/PID 1b8e:c003, stage=TPL)
Linux reboot update --> env update --> run usb_burning --> update 1000 --> тот же gadget (timeout 1 s)
sticky2==0x1b8ec003 --> board_late_init --> aml_try_factory_usb_burning(1) --> тот же gadget
```

---

## 2. USB: идентификация и дескрипторы

Источник: `v2019/usb_pcd.c:54-160` (git HEAD `bl33/drivers/usb/gadget/v2_burning/v2_usb_tool/usb_pcd.c`).

### 2.1 Device descriptor (строки 65-84)

| Поле | Значение | Строка |
|---|---|---|
| bLength / bDescriptorType | 18 / 0x01 | 66-67 |
| bcdUSB | **0x0200** (USB 2.0), при `USE_FULL_SPEED` — 0x0110 | 68-72 |
| bDeviceClass | 0x00 `USB_CLASS_PER_INTERFACE` | 73 |
| bDeviceSubClass / bDeviceProtocol | 0 / 0 | 74-75 |
| bMaxPacketSize0 | **64** | 76 |
| idVendor | **0x1B8E** | 54, 77 |
| idProduct | **0xC003** | 55, 78 |
| bcdDevice | **0x0007** | 79 |
| iManufacturer / iProduct / iSerialNumber | **0 / 0 / 0** (строки в дескрипторе не используются!) | 80-82 |
| bNumConfigurations | 1 | 83 |

Строки 1..3 (`"Amlogic"`, `"M8-CHIP"`, `"20110406"`) в коде есть (строки 155-226), но индексы
в дескрипторе нулевые — то есть **target строки не отдаёт**, tool их не запрашивает.
`_PLATFORM_CHIP_INDEX '8'` (строка 182) — единственный «переменный» символ.

### 2.2 Конфигурация (строки 86-160)

`wTotalLength = 32` (9 + 23), 1 интерфейс, `bConfigurationValue = 1`, `iConfiguration = 0`,
`bmAttributes = 0xC0` (`ONE | SELFPOWER`), `bMaxPower = 1` (2 мА).

Интерфейс 0: `bNumEndpoints = 2`, `bInterfaceClass = 0xFF` (vendor specific),
subclass/protocol = 0.

| EP | Адрес | Тип | MPS |
|---|---|---|---|
| bulk IN | **0x81** (`0x80|BULK_IN_EP_NUM`, `BULK_IN_EP_NUM=1`) | 0x02 | 512 (HS) / 64 (FS) |
| bulk OUT | **0x02** (`0x00|BULK_OUT_EP_NUM`, `BULK_OUT_EP_NUM=2`) | 0x02 | 512 (HS) / 64 (FS) |

Номера EP: `v2019/dwc_pcd.h:39-40`; MPS: `dwc_pcd.h:42-46`.
`USE_FULL_SPEED` включается только при `CONFIG_AML_USB_BURN_FULL_SPPED` (`dwc_pcd.h:33-35`),
а этого макроса **нет** в `yandex_station_v1.h` (проверено grep) → на Yandex Station Max это
**high-speed, MPS 512**, `bcdUSB 0x0200`.

Super-speed отсутствует: контроллер работает только как HS-устройство через DWC2 (`CONFIG_USB_DEVICE_V2`,
`yandex_station_v1.h:571`), дескриптора BOS/SS-capability нет.

### 2.3 Что требует tool

* Совпадение VID/PID `1b8e:c003` (иначе Windows-драйвер не тот; pyamlboot ищет ровно эту пару:
  `pyamlboot/pyamlboot.py:58`).
* Bulk IN на адресе 1, bulk OUT на адресе 2 (иначе `find_descriptor` в pyamlboot выберет другие EP;
  вендорский tool, судя по коду, тоже не ищет «любой bulk», а следует зафиксированным номерам).
* Интерфейс ровно один, vendor-specific.

---

## 3. Пакеты и транспорт

Транспорт = **vendor-специфичные control-запросы на EP0** (они задают адрес/длину/режим) +
**bulk-передача данных** на EP 0x02 (OUT) / 0x81 (IN). Никакого собственного заголовка кадра
(«magic/len/crc в начале пакета») в этом протоколе **нет** — в отличие от BootROM-команд `AMLC`
(cм. §7). Метаданные передаются в `setup`-пакете и в отдельной структуре команды.

### 3.1 Диспетчеризация на EP0

`do_vendor_request()` — `v2019/usb_pcd.c:459-621`, `do_vendor_out_complete()` — `:629-765`,
`do_vendor_in_complete()` — `:772-820`.

### 3.2 Коды запросов

`v2019/usb_pcd.h:41-67` (git HEAD `bl33/drivers/usb/gadget/v2_burning/v2_usb_tool/usb_pcd.h`):

| Код | Имя | Направление | `bmRequestType` | Что делает target | Строки |
|---|---|---|---|---|---|
| 0x01 | `AM_REQ_WRITE_MEM` | OUT | 0x40 | `memcpy((void*)(wValue<<16\|wIndex), data, wLength)` — прямая запись в ФА памяти | usb_pcd.c:470-480, 642-648 |
| 0x02 | `AM_REQ_READ_MEM` | IN | 0xC0 | читает `wLength` байт из `wValue<<16\|wIndex` | 481-494 |
| 0x03 | `AM_REQ_FILL_MEM` | OUT | 0x40 | payload = пары `{u32 addr, u32 val}`; пишет по адресам | 510-519, 649-658 |
| 0x04 | `AM_REQ_MODIFY_MEM` | OUT | 0x40 | `opcode=wValue`, payload `{addr,data,mask,addr2}`, 8 операций (RMW/copy/memcpy `data` слов) | 512-519, 667-670, 1007-1056 |
| 0x05 | `AM_REQ_RUN_IN_ADDR` | OUT | 0x40 | выключает PHY и прыгает на `wValue<<16\|wIndex` | 520-529, 671-681 |
| 0x06 | `AM_REQ_WRITE_AUX` | OUT | 0x40 | `DWC_REG_BASE + (wValue<<16\|wIndex) = u32` (регистр ядра USB) | 511-519, 659-666 |
| 0x07 | `AM_REQ_READ_AUX` | IN | 0xC0 | заглушка: возвращает нули (4 байта) | 495-509 |
| 0x11 | `AM_REQ_WR_LARGE_MEM` | OUT | 0x40 | payload 16 Б `{u32 addr, u32 len, 0, 0}`; затем bulk OUT | 530-537, 682-701 |
| 0x12 | `AM_REQ_RD_LARGE_MEM` | IN | 0xC0 | тот же payload; затем bulk IN | 538-550 |
| 0x20 | `AM_REQ_IDENTIFY_HOST` | IN | 0xC0 | 4+ байта версии/стадии | 573-589 |
| 0x30 | `AM_REQ_TPL_CMD` | OUT | 0x40 | ASCII-команда → `optimus_working()` | 602-608, 738-752 |
| 0x31 | `AM_REQ_TPL_STAT` | IN | 0xC0 | отдаёт 512-байтовый `_resultInfo` | 609-616, 781-782 |
| 0x32 | `AM_REQ_DOWNLOAD` | OUT | 0x40 | команда записи образа + bulk OUT | 551-562, 702-737 |
| 0x33 | `AM_REQ_UPLOAD` | IN | 0xC0 | выгрузка из eMMC/памяти + bulk IN | 563-572, 789-812 |
| 0x34 | `AM_REQ_BULKCMD` | OUT | 0x40 | «текстовая команда + статус в bulk IN» | 590-601, 753-764 |

Стандартные запросы: `GET_DESCRIPTOR` (`v2019/usb_pcd.c:323-...`), `SET_CONFIGURATION` (`:422-437`,
снимает таймаут ожидания enumerate), `GET_CONFIGURATION` (`:438-...`), `SET_ADDRESS` — в UDC-слое
(`dwc_pcd_irq.c:199-210`).

### 3.3 Identify (0x20)

`v2019/usb_pcd.c:573-589`, значения из `platform.h:125-133`:

```c
static const char id[4] = { USB_ROM_VER_MAJOR, USB_ROM_VER_MINOR, USB_ROM_STAGE_MAJOR, USB_ROM_STAGE_MINOR };
// TPL(U-Boot): {0, 7|8, 0, 16}
```

pyamlboot читает **8** байт (`pyamlboot/pyamlboot.py:318-325`) и трактует: `major`, `minor`,
`stage_major`, `stage_minor` (0=IPL/BootROM, 8=SPL, 16=TPL), `raw[4]` = need_password,
`raw[5]` = password_ok (`pyamlboot/socid.py`). Печать на target: `printf("\nID[%d]\n", _pcd->buf[3])`.

**Ключевой факт для порта:** именно `stage_minor == 16` заставляет tool пропустить загрузку
BL2/AMLC и сразу перейти к текстовым командам (`pyamlboot/optimus.py:504-...`,
`BurnStepDownloadUboot.do`: `if socid.stage_minor == SocId.STAGE_MINOR_TPL: 'No need download UBOOT'`).
Если отдавать 0 — tool полезет загружать DDR-прошивку и BL2 через AMLC (нам это не нужно).

### 3.4 Bulk-обмен: правила и контроль потока

* Внутренняя нарезка: `DWC_BLK_MAX_LEN = 8*BULK_EP_MPS` = **4096 Б** (HS), при FS — `6*MPS` — один вызов
  `dwc_otg_ep_req_start()`; `DWC_BLK_LEN()/DWC_BLK_NUM()` (`v2019/usb_pcd.c:44-52`).
* Для `0x11/0x12`: `bulk_len = wValue` (длина блока), `bulk_num = wIndex` (число блоков),
  данные идут следом (`v2019/usb_pcd.c:530-550`, `682-701`).
* Для `0x32 DOWNLOAD`: структура команды в EP0-данных (`usb_pcd.h:70-80`):

```
struct CmdData_WriteMedia {       // v2019/usb_pcd.h:70-78, pack(4), 32 байта
  u32 retryTimes;                 // +0
  u32 userDataLen;                // +4   длина этого пакета данных
  u32 sequenceNo;                 // +8   номер пакета (с 0)
  u32 dataCheckSum;               // +12
  u16 dataCheckAlgorithm;         // +16  0x00EE none, 0x00EF add-sum, 0x00F0 crc32
  u16 bulkInAckLen;               // +18  обычно 0x200
  u8  reserv[12];                 // +20
};
```

  Разбор — `v2019/usb_pcd.c:702-737`: `isPktResended = intBuf[0]`, `len = intBuf[1]`,
  `seq = intBuf[2]`, `sum = intBuf[3]`; если `wLength == 0x20` («new protocol») дополнительно
  `alg = intBuf[4] & 0xffff`, `ackLen = intBuf[4] >> 16`; иначе `alg = ADDSUM`, `ackLen = 512`.
  При `wIndex == 0xffff && wLength == 0x20` включается режим «отвечать на каждый пакет»
  (`:551-562`).
* Проверка суммы — `bulk_transfer_reply()` (`v2019/usb_pcd.c:824-896`): при `ADDSUM` считается
  `add_sum(data, len)` и сравнивается с `origiSum`; при несовпадении — пакет отклоняется с текстом
  `"Alg[%x]0x: genSum %x != org %x at seq %x, len %x, resend %d"`.
  **Реализации `add_sum()` в дереве нет** — только объявление (`bl33/.../v2_common/optimus_download.h:159`).
  Рабочий эквивалент из pyamlboot (`pyamlboot/pyamlboot.py`, `BurnStepDownloadUboot._chksum`):
  сумма 32-битных little-endian слов по модулю 2^32, хвост 1..3 байта маскируется
  (`0xffffff` / `u16` / `u8`). Это же значение tool кладёт в `dataCheckSum`.
* Ответ target’а (`v2019/usb_pcd.c:851-878`): в bulk IN пишется `ackLen` (по умолчанию 512) байт;
  при успехе — `"OK!!"` (`:878`), иначе текст ошибки (`"media error %d at seq %d..."`,
  `"Alg[...]genSum..."`), см. `:824-870`.
* Долгий прожиг: пока target занят, он вместо ответа шлёт `"Continue:32"` (для DOWNLOAD) или
  `"Continue:34"` (для BULKCMD) не чаще, чем раз в 16 с и не раньше 8 МБ данных
  (`v2019/usb_pcd.c:1086-1095` — строки, `:1103-1132` — `platform_busy_increase_un_reported_size`). Tool обязан
  повторять чтение, пока не увидит `OK!!`/итог (pyamlboot: цикл по `Continue:32`, таймаут 10 с,
  ресенд до 3 раз: `optimus.py:565-600`).
* Требование к размеру: **все пакеты, кроме последнего, кратны 64 КБ** — иначе target отвергает
  (`bl33/.../v2_common/optimus_buffer_manager.c:190-193`). pyamlboot использует
  `block_size = 0x10000` (`optimus.py:604-620`).
* Буфер в target: 64 МБ (`optimus_download.h:74-99`), запись на носитель — «слот-менеджером» по
  64 КБ (`optimus_buffer_manager.c:222-305`), для `sparse` — накопление всего образа
  (`writeBackUnitSz = 64 МБ`, `:101-105`).
* Ограничение адреса: `partBaseOffset` должен быть < 4 ГБ (`optimus_buffer_manager.c:131-135`),
  запись «в память» ограничена `OPTIMUS_MEMORY_WRITE_BACK_SZ = 2 ГБ` (`optimus_download.h:120`).

### 3.5 Текстовые команды (0x30/0x31 и 0x34)

* `0x30 TPL_CMD`: `wIndex` — субкод, `wIndex==1` → выполнить строку; строка копируется
  из EP0-буфера (`CMD_BUFF_SIZE = 512`, `usb_pcd.h:33`), **обязан быть NUL-терминатор**;
  если команда начинается с `bootm`, target отключает pull-up (`v2019/usb_pcd.c:738-752`).
  Ограничение pyamlboot: < 127 символов, иначе «U-Boot's USB function gets confused»
  (`pyamlboot.py:327-340`).
* Результат — `0x31 TPL_STAT`: 512 байт, `"success"` или `"failed:<текст>"`
  (`v2019/optimus_transform.c:474-487`).
* `0x34 BULKCMD`: то же самое, но команда приходит в EP0-данных, а ответ — в bulk IN 512 байт
  (`v2019/usb_pcd.c:968-1005`). Так работает «длинный» путь и все `download`-команды.

---

## 4. Команды протокола

### 4.1 Диспетчер текстовых команд

`optimus_working()` — `v2019/optimus_transform.c:362-488`. Неизвестная команда **проваливается в
`run_command(cmd, 0)`** (`:468-470`) — то есть через 0x30/0x34 можно выполнить любую команду
U-Boot. Для mainline-порта это надо явно запретить.

| Команда (argv) | Аргументы | Что делает | Строки |
|---|---|---|---|
| `low_power` | — | гасит LCD, ставит лимит 500 мА | 375-379, 264-... |
| `disk_initial <erase>` | 0/1 | инициализация eMMC/разметки, возможно со стиранием | 380-384 |
| `bootloader_is_old` | — | `is_tpl_loaded_from_usb()`; на USB-загруженном TPL всегда «failed» → tool считает bootloader новым и **не стирает** | 385-390 |
| `erase_bootloader` | — | `optimus_erase_bootloader("usb")` → `store_erase_ops("boot")` (SCPI-ветка выключена) | 391-400; `optimus_download.c:1398-1412` |
| `write_raw_img <part> <addr> <off> <size>` | | запись из ОЗУ в раздел; для `part==bootloader` — фиксированные 0x60000 Б | 402-405, 46-80 |
| `read_raw_img ...` | — | **удалена в 2019** (была в 2017) | — |
| `simg2part <part> <addr> <size>` | | распаковать sparse из ОЗУ в раздел | 406-409, 77-... |
| `download <media> <part> <type> <size>` | см. §4.2 | подготовка приёма образа | 411-414 |
| `upload <media> <part> <type> <size>` | | подготовка выгрузки | 411-414 |
| `download get_status` | | `!is_optimus_burn_complete()` → «success» когда образ принят полностью | `optimus_download.c:1122-1125` |
| `download is_ready` | | `!is_optimus_to_burn_ready()` | 1127-1130 |
| `verify sha1sum <40 hex>` | | sha1 раздела/образа и сравнение | 1043-1089 |
| `sha1sum <part> <len> <hex>` | | то же с явными аргументами | `optimus_transform.c:129-...` |
| `save_setting` | — | `setenv upgrade_step 1..2; firstboot=1; saveenv` | `optimus_download.c:1229-1271` |
| `burn_complete <choice>` | 0..3, 0xdc, 0xeb, 0xe1 | poweroff / reboot normal / reboot update / query | `optimus_download.c:1328-...`, константы `optimus_download.h:196-202` |
| `reset` | — | `reboot normal` + `reset_cpu` | `optimus_transform.c:410-414`, `optimus_download.c:1298-1316` |
| `poweroff` | — | `optimus_poweroff()` (без CONFIG_POWER_KEY — «нажмите Ctrl-C») | `optimus_transform.c:415-418` |
| `md <addr> <len>` | | hexdump памяти | 419-421, 276-299 |
| `key ...` | | efuse/secure key (unifykey) | 423-426, `optimus_download_key.c` |
| `support_tempcontrol`, `tempcontrol <min> <max>`, `get_chipid` | | температура/chip id | 421-440, 306-361 |
| `echo …` | | есть в списке шагов pyamlboot: «    echo 1234» — выравнивание 4 байт после reset | `optimus.py:111-120` |

### 4.2 `download` / `upload`: параметры

Разбор — `bl33/.../v2_common/optimus_download.c:1111-1166`, `_parse_img_download_info` (:740-...).

| Аргумент | Допустимые значения |
|---|---|
| `media` | `store`, `nand`, `sdmmc`, `spiflash`, `key`, `mem` (+ `get_status`, `is_ready`) — `:1122-1151`, `:659-687` |
| `part` | для `store` — имя раздела (`boot`, `system`, `bootloader`, `_aml_dtb`, `dtb`); для `mem` — адрес в hex или `dtb`; для `key` — имя ключа |
| `type` | `normal`, `sparse`, `bootloader`, `dtb`, `ubifs` — `:634-650`, enum `IMG_TYPE_*` (`:77-83`: sparse=0xAE, normal=0xAF, bootloader=0xB0, dtb=0xB1, ubifs=0xB2) |
| `size` | десятичное, размер файла в байтах |

Особые случаи:
* `part == bootloader` → пишется через `store_boot_write()` (сырой BL2/FIP-образ), макс. 2 МБ
  (`optimus_download.c:204-217`), выравнивание `BOOTLOADER_ALIGN_BITS`.
* `part == dtb` (media `mem`) → образ складывается по адресу `OPTIMUS_DTB_LOAD_ADDR` (0x1000000)
  и потом пишется в раздел `dtb` (`optimus_download.c:494-505`, `752-761`).
* `type == sparse` → на лету разбирается AOSP sparse (`sparse_format.h`: magic `0xed26ff3a`,
  чанки `0xCAC1 RAW`, `0xCAC2 FILL`, `0xCAC3 DONT_CARE`; `CHUNK_TYPE_CRC32 (0xCAC4)` —
  «unsupported yet!», `optimus_simg2img.c:281-283`).

### 4.3 Полная последовательность, которую выполняет tool (по pyamlboot)

`pyamlboot/optimus.py:745-830` (`get_burn_steps`), при `stage_minor == 16` (наш случай):

```
0x20 identify                     -> {0,7,0,16}
0x34 "    echo 1234"              (обход потери 4 байт после reset)   [опционально]
0x34 "    low_power"
0x34 "disk_initial 0"             (таймаут 60 с)
для каждого item в пакете:
    0x30 "download <media> <part> <type> <size>"
    0x32  N пакетов по 64 КБ (данные) + bulk IN ack 0x200
    0x34 "download get_status"
    если есть VERIFY-элемент: 0x34 "verify sha1sum <sha1>"
0x34 "save_setting"
0x34 "burn_complete 1|3"
```

Проверка «bootloader_is_old / erase_bootloader / reset» — отдельный предварительный шаг
(`pyamlboot/optimus.py:95-140`), по умолчанию в pyamlboot его можно отключить.

---

## 5. Формат образа Amlogic v2 (AMLIMAGE)

### 5.1 Заголовок (`bl33/.../v2_common/amlImage_if.h:34-82`, pack(4), 64 байта)

```c
typedef struct {
    u32 crc;            // +0  «check sum of the image»
    u32 version;        // +4  1 или 2 (AML_FRMWRM_VER_V1/V2)
    u32 magic;          // +8  0x27b51956 (IMAGE_MAGIC)
    u64 imageSz;        // +12 общий размер файла
    u32 itemAlginSize;  // +20
    u32 itemNum;        // +24 (target принимает не больше MAX_ITEM_NUM = 48)
    char reserve[36];   // +28
} AmlFirmwareImg_t;
```

### 5.2 Таблица элементов

Сразу за заголовком, `itemNum` записей фиксированного размера:

| Версия | Размер записи | Имена | Подтверждение |
|---|---|---|---|
| V1 (`version==1`) | **128 Б** | `ITEM_NAME_LEN_V1 = 0x20` | `amlImage_if.h:38,44-54`; `COMPILE_TYPE_CHK(128 == sizeof(ItemInfo_V1))` — `optimus_img_decoder.c` |
| V2 (`version==2`) | **576 Б** | `ITEM_NAME_LEN_V2 = 0x100` | `amlImage_if.h:39,58-68`; `COMPILE_TYPE_CHK(576 == sizeof(ItemInfo_V2))` |

Поля записи: `itemId`, `fileType` (0x00 normal / 0xFE sparse), `curoffsetInItem`, `offsetInImage`,
`itemSz`, `itemMainType[]`, `itemSubType[]`, `reserve[32]`.
Данные элементов идут по **абсолютным** `offsetInImage`; порядок обязан совпадать с `itemId`
(`optimus_img_decoder.c`, `image_item_open`: `if (i != pItem->itemId) error`).

### 5.3 Реальный пакет Yandex Station Max

Разобран `yandex_s905x3_custom_v130.img` (sha256
`eb85e390746cc472b64fdfbea53f2cea790843fdd95ec8e010c43341fe335ed1`, 1 566 609 544 Б):

```
crc=0xbc03b96b version=2 magic=0x27b51956 imageSz=1566609544 itemAlginSize=4 itemNum=17
таблица: 64 + 17*576 = 0x268  (первый элемент начинается ровно там же)

 id  type   offset      size        main/sub
  0  0x00   0x0002680   0x162f70    USB/DDR
  1  0x00   0x0002680   0x162f70    USB/UBOOT
  2  0x00   0x01655f0   0x017800    PARTITION/_aml_dtb
  3  0x00   0x017cdf0   0x000030    VERIFY/_aml_dtb       "sha1sum 2b4bd7c9b0880010cc4765c83c402f4b18435f3b"
  4  0x00   0x017ce20   0x163170    UBOOT/aml_sdc_burn
  5  0x00   0x02dff90   0x00025a    ini/aml_sdc_burn
  6  0x00   0x02e01ec   0x9e2000    PARTITION/boot        "ANDROID!" (Android boot image)
  7  0x00   0x0cc21ec   0x000030    VERIFY/boot
  8  0x00   0x0002680   0x162f70    PARTITION/bootloader  (тот же образ, что USB/UBOOT)
  9  0x00   0x0cc221c   0x000030    VERIFY/bootloader
 10  0x00   0x0cc224c   0x146d40    PARTITION/logo        "AML_RES!" logo
 11  0x00   0x0e08f8c   0x000030    VERIFY/logo
 12  0x00   0x01655f0   0x017800    dtb/meson1
 13  0x00   0x01655f0   0x017800    dtb/meson1_ENC
 14  0x00   0x0e08fbc   0x00009b    conf/platform
 15  0x00   0x0e09058   0x5c800000  PARTITION/system    (1.48 ГБ, normal, не sparse)
 16  0x00   0x5d609058  0x000030    VERIFY/system
```

`conf/platform` (155 Б, текстовый INI):

```
Platform:0x0811
DDRLoad:0xfffa0000
DDRRun:0xfffa0000
DDRSize:0x10000
Control0=0xfffa0000:0x00000004
Control1=0xfffa0000:0x00005183
Encrypt_reg:0xff800228
```

`0x0811` — идентификатор SM1; `DDR*`/`Control*` нужны только для BootROM-пути (заливка DDR-прошивки),
для TPL-пути не используются.

`VERIFY/<part>` — ASCII-строка `sha1sum <40 hex>`; именно она попадает в
`verify sha1sum <sum>` (`optimus_download.c:1043-1089`).

### 5.4 Что target обязан уметь, чтобы образ считался валидным

1. Отдать `identify` со `stage_minor = 16` (иначе tool пойдёт по BootROM-ветке с AMLC).
2. Принять `download store <part> normal|sparse|bootloader|dtb <size>` для каждого PARTITION/
   `dtb`-элемента. **Важно:** `PARTITION/_aml_dtb` идёт как раздел с именем `_aml_dtb`,
   а `PARTITION/bootloader` (а не `USB/UBOOT`) — как сырой загрузчик.
3. Ответить `"OK!!"` на каждый 64-КБ пакет и `"success"` на `download get_status` после
   последнего пакета образа (проверка `imgSzDisposed == imgPktSz`).
4. Посчитать sha1 по записанному разделу и сравнить с `VERIFY/*` (иначе tool сообщит об ошибке
   верификации; верификация включается по наличию элемента).
5. `save_setting` + `burn_complete <n>` — иначе tool не увидит «Burning successfully».

Чего target **не** обязан делать: проверять поле `crc` заголовка — реализация `image_check()`
в дереве отсутствует (есть только прототип `amlImage_if.h:105`), и пакет прошивается
и без неё.

---

## 6. Что реализовать в mainline U-Boot

### 6.1 Опорные точки в mainline

`mainline/u-boot/yandex-station-max_defconfig` уже содержит:

```
CONFIG_USB=y
CONFIG_USB_GADGET=y
CONFIG_USB_GADGET_DWC2_OTG=y
CONFIG_USB_GADGET_DWC2_OTG_PHY_BUS_WIDTH_8=y
CONFIG_USB_GADGET_DOWNLOAD=y
CONFIG_USB_GADGET_VENDOR_NUM=0x1b8e
CONFIG_USB_GADGET_PRODUCT_NUM=0xfada
CONFIG_USB_DWC3=y / CONFIG_USB_DWC3_MESON_G12A=y   (host)
```

То есть **UDC-драйвер (`dwc2_udc_otg`) на SM1 уже выбран**, но VID/PID надо поменять на
`0x1b8e:0xc003` и вместо `gadget download` (ums/thor) подключить свой gadget-функционал.
Порт не требует нового UDC — только новый «function» + команду входа.

### 6.2 Минимальный набор (достаточно, чтобы tool прошил образ)

| Что | Обязательность | Комментарий |
|---|---|---|
| EP0: `GET_DESCRIPTOR` (device/config/string), `SET_CONFIGURATION`, `GET_STATUS` | must | `bcdUSB 0x0200`, MPS0 64, 1 интерфейс, 2 bulk EP |
| `0x20 IDENTIFY_HOST` → 8 байт `{0,7,0,16,0,0,0,0}` | must | без `stage=16` tool уйдёт в AMLC-ветку |
| `0x30 TPL_CMD` + `0x31 TPL_STAT` | must | основной путь команд (у pyamlboot), stat = 512 Б |
| `0x34 BULKCMD` (команда + 512-Б статус в bulk IN) | must | этим путём идут `download`/`verify`/`burn_complete` |
| `0x32 DOWNLOAD` + bulk OUT, ack 512 Б в bulk IN | must | 64-КБ пакеты, add-sum 0xEF, `"OK!!"`, `"Continue:32"` |
| `0x33 UPLOAD` (выгрузка раздела) | optional | нужен только для `readback`/`mread` |
| `0x01/0x02 WRITE/READ_MEM`, `0x05 RUN_IN_ADDR` | optional | нужны tool’у только для BootROM-сценария и «run» |
| `0x11/0x12 LARGE_MEM` | optional | альтернативный путь записи в ОЗУ |
| `0x03/0x04/0x06` (fill/modify/aux) | not needed | низкоуровневые трюки PC-тула |

Команды (минимум):

```
"    low_power"                 -> 0
"disk_initial <n>"              -> инициализация MMC + разметки (в mainline: mmc init + part scan)
"download store <part> <type> <size>"  -> подготовка буфера/оффсета
"download get_status"           -> "success" когда всё принято
"download is_ready"             -> "success"
"verify sha1sum <hex>"          -> sha1 по записанному
"save_setting"                  -> no-op/«success» (env в mainline не персистентен!)
"burn_complete <n>"             -> reboot/poweroff
"bootloader_is_old"             -> "failed:..." (пусть tool считает bootloader новым)
"reset"                         -> do_reset()
```

Нереализуемое/ненужное: `erase_bootloader` (в вендоре стирает раздел `boot`), `key` (efuse/secure),
`simg2part`, `md`, `tempcontrol`, `get_chipid`, `ubifs`, `nand`, `spiflash`, `sdmmc`,
SCPI-переключения, `AMLC`/`0x50`/`0x60`, `0x35 PASSWORD`, `0x36 NOP`.

### 6.3 Что именно писать (архитектурно)

1. `drivers/usb/gadget/f_amlburn.c` — обработчик EP0 vendor-запросов + два bulk-EP, реализованный
   на **legacy UDC API** (`struct usb_gadget_driver`, `setup`/`disconnect`/`suspend`, плюс прямые
   вызовы `usb_ep_queue`) — так же, как это делает вендорский `usb_pcd.c`, но поверх
   `dwc2_udc_otg`.
2. Очередь завершений: `dwc2_udc_otg` даёт `req->complete`; вендорский код вместо этого опрашивает
   `usb_pcd_irq()` в цикле — в mainline нужен event-loop `usb_gadget_handle_interrupts()`.
3. `board/amlogic/yandex-station-max/yandex-station-max.c`: в `board_init()`/`misc_init_r()`
   повторить гейт `PREG_STICKY_REG2` (адрес `0xff634400 + 0x1c8`) и env `usb_burning`, но
   **только по кнопке/GPIO или env**, без безусловного входа.
4. Запись на носитель: `blk_dwrite()` по `struct disk_partition` из `part_get_info_by_name()`
   вместо вендорского `store_write_ops()`; sparse-разбор (AOSP) — отдельный маленький парсер
   (`CHUNK_TYPE_RAW/FILL/DONT_CARE`, `sparse_format.h`).
5. `bootloader`-элемент: писать как есть (FIP BL2+BL33) в начало раздела `bootloader`,
   предварительно проверив подпись (см. §9).
6. Env: в mainline `CONFIG_ENV_IS_NOWHERE` (см. `docs/uboot.md`, §1) → `save_setting`
   и `burn_complete` не должны полагаться на `setenv/saveenv`. Достаточно вернуть «success».

---

## 7. Отличия от BootROM-режима (pyamlboot)

| Аспект | BootROM (IPL, stage 0) | U-Boot TPL (stage 16) |
|---|---|---|
| VID/PID | те же `1b8e:c003` (`pyamlboot.py:58`) | те же |
| Набор vendor-запросов | тот же базовый (0x01…0x34) + `0x35 PASSWORD`, `0x36 NOP`, `0x50 GET_AMLC`, `0x60 WRITE_AMLC` (`pyamlboot.py:17-46`) | без AMLC/password (в `usb_pcd.h` их нет) |
| Как заливается загрузчик | tool сам заливает DDR-прошивку + BL2: «para»-структура `{magic 0x3412cdab, 0x200, 0xc0e0, …, UbootLoad, size, chksum}`, `RUN_IN_ADDR`, затем AMLC-блоки по 0x200/0x4000/0x10000 (`optimus.py:428-520`, `pyamlboot.py:367-495`) | не нужно: `BurnStepDownloadUboot.do` выходит сразу при `stage_minor==16` |
| Управление | «сырые» запросы + AMLC-стрим | текстовые команды `download/verify/burn_complete` |
| Сброс/перезагрузка | `RUN_IN_ADDR` на точку входа | `0x05`/`reset`/`burn_complete` |

**Следствие для mainline:** нам нужен только TPL-профиль. Достаточно корректно отдавать
`identify` со `stage_major=0, stage_minor=16` и поддерживать 0x20/0x30/0x31/0x32/0x34.
BootROM-часть (AMLC, BL2, DDR) реализовывать не надо — она уже в ROM, и tool её пропускает,
как только видит TPL.

---

## 8. План проверки на железе

### 8.1 Подготовка

1. Собрать U-Boot с `f_amlburn` (VID/PID `1b8e:c003`), залить на устройство штатным путём.
2. Войти в режим: `update 1000` из консоли либо кнопкой (если реализуем), либо `reboot update` из Linux.

### 8.2 Что смотреть на Linux-хосте

```
lsusb -v -d 1b8e:c003        # bcdUSB 2.00, idVendor 0x1b8e, idProduct 0xc003, bcdDevice 0x0007
                             # bMaxPacketSize0 64, bConfigurationValue 1, bmAttributes 0xc0, MaxPower 1
                             # Interface 0: bInterfaceClass 0xff, bNumEndpoints 2
                             # EP 0x81 Bulk 512, EP 0x02 Bulk 512, bNumConfigurations 1, iManufacturer/iProduct 0
dmesg | tail                 # "new high-speed USB device number N using xhci_hcd", "idVendor=1b8e, idProduct=c003"
                             # скорость: 480 Mbps (high), НЕ "full-speed"
```
Нет `iManufacturer`/`iProduct` — это ожидаемо (в дескрипторе нули), не считать багом.

Трасса протокола (Linux): `modprobe usbmon`, затем Python-скрипт на `pyusb` (или `pyamlboot`
как хост) с `usbmon` — так можно получить точные `wValue/wIndex/wLength` от конкретного tool’а.

### 8.3 Что смотреть в Windows (UsbTreeView / Wireshark+USBPcap)

* Дерево дескрипторов: сверить с §2 (в первую очередь `bcdUSB`, MPS, порядок EP).
* Первый vendor-запрос после `SET_CONFIGURATION` — должен быть `0x20` c 8-байтовым ответом,
  четвёртый байт `16`.
* Последовательность control-запросов: `0x34`/`0x30` → `0x32` с 32-байтовой структурой →
  bulk OUT 0x10000 → bulk IN 0x200.
* Retry-поведение при намеренно испорченной сумме (проверка `add_sum`).

### 8.4 Тестовые образы (по возрастанию сложности)

| # | Образ | Что проверяет |
|---|---|---|
| 1 | самодельный пакет: header + 1 элемент `PARTITION/boot` (нормальный) | download + get_status |
| 2 | пакет: `bootloader` + `_aml_dtb` + `boot` + `VERIFY/*` | bootloader-путь, mem→dtb, sha1 |
| 3 | `yandex_s905x3_custom_v130.img` (реальный, 1.5 ГБ) | полный цикл, 64-КБ поток, скорость |
| 4 | тот же пакет с `sparse`-system (пересобрать) | sparse-разбор `RAW/FILL/DONT_CARE` |

Критерии успеха: tool доходит до «Burning successfully»; `download get_status` → `success`;
`verify sha1sum` совпал; после `burn_complete 1` устройство грузится; побайтовое сравнение
записанных разделов с образами (`dd` + `sha256sum`).

### 8.5 Что мерить

* Скорость: ожидание ≥ 20 МБ/с на HS bulk (вендор пишет «64 КБ пакетами»); при 4-КБ нарезке
  внутри UDC будет заметно медленнее — сравнить.
* Таймауты tool’а: если между пакетами > 10 с, pyamlboot-подобный хост ругается; Windows-tool
  ведёт себя мягче, но `Continue:32` нужно слать, иначе tool решит, что target умер.

---

## 9. Открытые вопросы

1. **Кто пишет `PREG_STICKY_REG2 = 0x1b8ec003`** и зачем два значения (0xc003/0xc004)?
   В дереве нет ни одной записи. Как добыть: снять дамп регистра до/после прошивки
   (`md 0xff6345c8` в консоли U-Boot), либо трасса `0x01 WRITE_MEM` в UsbTreeView.
2. **Точные `wValue`/`wIndex` от Windows-тула** для `0x32` (в исходниках они не используются,
   кроме частного случая `wIndex == 0xffff && wLength == 0x20`). Как добыть: USBPcap/usbmon.
3. **Алгоритм поля `crc` заголовка пакета** (у v130 `0xbc03b96b`). Проверенные гипотезы
   (crc32 файла, crc32 тела с 0x268, crc32 заголовка с обнулённым полем, no-final-xor) —
   не совпали. В BSP реализация `image_check()` отсутствует. Влияние: похоже, target это поле
   не проверяет; подтвердить трассой.
4. **Реализация `add_sum()`** в дереве отсутствует (только прототип). Использован эквивалент из
   pyamlboot (сумма u32-слов). Нужно подтвердить на реальном пакете с искажённой суммой.
5. **`BULK_EP_MPS` на конкретном tool’е**: вендорский код допускает FS (64 Б) при
   `CONFIG_AML_USB_BURN_FULL_SPPED`; для Yandex макрос не задан, но подтвердить
   (`lsusb -v` на вендорской прошивке) стоит.
6. **PID `0xC004`**: в BSP не встречается в дескрипторе, но проверяется в `board_init`.
   Возможно, это «новая» ревизия tool’а. Как добыть: `lsusb` при прошивке вендорским tool’ом.
7. **Кнопка/GPIO**: у Yandex Station Max вход в burn, судя по коду, только по sticky-регистру
   и env; отдельной кнопки в BSP нет. Проверить схему (кнопка `reset`?) и поведение при
   удержании.
8. **Подпись**: пакет содержит `bootloader`-элемент как FIP с подписью; на устройстве
   с включённым secure boot (efuse) незаподписанный BL2/BL33 не загрузится. Нужно понять,
   подписан ли конкретный экземпляр (`0x1b8ec003` как маркер?), и какие ключи в
   `bl33/board/amlogic/yandex_station_v1/aml-user-key.sig`.

---

## 10. Риски

| Риск | Почему | Что делать |
|---|---|---|
| Дескрипторы | tool ищет именно `1b8e:c003` (pyamlboot это подтверждает); другой PID → «нет устройства» в Windows | зафиксировать VID/PID, MPS 512, EP 1 IN/2 OUT, `bcdUSB 0x0200` |
| Скорость | UDC mainline + 4-КБ блоки может дать < 5 МБ/с; tool имеет таймауты | крупные `usb_ep_queue` (64 КБ), не дробить на 4 КБ; мерить |
| Отсутствие исходников tool’а | Windows-бинарник не анализировался; поведение известно только по target-коду и pyamlboot | сверять по usbmon/UsbTreeView |
| Env не сохраняется (mainline) | `upgrade_step`, `firstboot` — вендорская логика «первой загрузки» | `save_setting`/`burn_complete` как no-op, состояние — в отдельный раздел/efuse |
| Подпись BL2/BL33 | secure boot может отвергнуть наш U-Boot | проверить efuse-биты; при необходимости — подписывать (`fip/stool`, `fip/g12a/aml_encrypt_g12a`) |
| `run_command()` fallback | вендорский диспетчер исполняет **любую** команду U-Boot по USB | в порте — белый список команд, иначе это удалённый shell |
| Стирание раздела `boot` | `erase_bootloader` в вендоре при выключенном SCPI стирает `boot` | реализовать как «failed», не стирать |
| `board_late_init` без скобок | `aml_try_factory_sdcard_burning()` вызывается всегда | не копировать баг |

---

## Приложение A. Быстрая шпаргалка «что отвечать»

```
GET_DESCRIPTOR(device)  -> 18 байт, VID 1b8e PID c003 bcdUSB 0200 bcdDevice 0007 MPS0 64
GET_DESCRIPTOR(config)  -> 32 байта: 1 intf 0xff, EP 0x81 IN 512, EP 0x02 OUT 512, MaxPower 1
SET_CONFIGURATION       -> ACK
0x20 IDENTIFY (IN, 8)   -> 00 07 00 10 00 00 00 00     ("ID[16]")
0x30 TPL_CMD  (OUT)     -> cmd+NUL, wIndex=1
0x31 TPL_STAT (IN,512)  -> "success\0" | "failed:<текст>\0"
0x34 BULKCMD  (OUT)     -> cmd+NUL, затем bulk IN 512: "success\0"/"failed:...\0"
0x32 DOWNLOAD (OUT,32)  -> {retry,len,seq,sum,alg=0xEF,ack=0x200,0...}
                           затем bulk OUT (len, кратно 64К кроме последнего)
                           затем bulk IN 0x200: "OK!!" | "Continue:32" | "failed..."
0x33 UPLOAD   (IN,512)  -> {0xefe8, len} затем bulk IN данные
```

---

## 14. Corrections after the full source review (2026-10-29)

Verified against the complete `v2_burning` tree in `git HEAD` and the
decompressed v112 BL33. These supersede the earlier statements:

1. `identify` (0x20) returns `wLength` bytes copied from a **4-byte** array
   `{0, 7, 0, 16}` (stage_minor = 16). An 8-byte request over-reads the next
   .rodata bytes (`89 4c 5a 4f`); a mainline port should return explicit
   `00 07 00 10 00 00 00 00`.
2. The transfer buffer is **32 MB**, not 64 MB (`CONFIG_AML_MTD=1`):
   `DDR_MEM_ADDR_START=0x1000000`, buffer `0x1400000..0x3400000`,
   `OPTIMUS_DTB_LOAD_ADDR=0x1000000`.
3. `erase_bootloader` **does not erase `boot`**: with
   `ROM_BOOT_SKIP_BOOT_ENABLED_4_USB=1` it issues SMC `FORCE_USB_BOOT`
   (`set_usb_boot_function(2)`); the `store_erase_ops("boot")` path is
   unreachable in this configuration.
4. `add_sum()` **is implemented** (`v2_common/optimus_progress.c:113-153`):
   sum of little-endian u32 words with the 1..3 byte tail masked. Only the
   AMLIMAGE header `crc` remains unknown (and the BSP never checks it).
5. `try_auto_burn` is not invoked in the Yandex configuration; the real entry
   points are USB boot, `reboot_mode=update`, `upgrade_step==3` and the
   sticky register. The `upgrade_key`/`rom_sdboot_key` scripts exist but are
   never called, so there is no working physical burn button.
6. `0x33 UPLOAD` is **not optional**: pyamlboot performs a secure-check with
   `upload mem <addr> normal 4`. For a non-secure board returning zeroes is
   enough.
7. Section naming: `PARTITION/bootloader` and `_aml_dtb` are recognised by
   **partition name**, not by the `download` type argument; `dtb/meson1` is
   sent as `download mem dtb normal <size>`, not as a partition.

Additional confirmed facts:

- BL33 in the v112 FIP decompresses to 1 438 432 bytes (sha256
  `db348386…57ba2b`); its device descriptor at offset `0xf3c2d` and config at
  `0xf1784` match the source (VID/PID, MPS0 64, EP 0x81/0x02 bulk 512).
- An AMLIMAGE package item `PARTITION/bootloader` is byte-identical to
  `u-boot.bin`; the package is parsed by the PC tool, the target only receives
  per-item `download`/`verify` commands and never opens the AMLIMAGE itself.
- `bootloader_is_old` returns success when booted from eMMC and failed when
  booted from USB. A mainline port must always answer **failed**, otherwise
  the tool erases and resets into the BootROM flow and flashes the vendor
  bootloader from the package.
