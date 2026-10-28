# Фронтальная LED-матрица 25x16 (Gowin FPGA): reverse engineering

Статус: **RE завершён настолько, насколько это возможно без железа.** Всё ниже —
из unstripped-модулей вендора (DWARF + дизассемблер), DTB/DTS вендора и
userspace. Всё, что не подтверждено кодом, помечено `[предположение]` /
`[проверить на железе]`.

Источники (`board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/`):

| Модуль | sha256 | .text | Файлы исходников (DWARF) |
|---|---|---|---|
| `gowin_led_device.ko` | `c020403c433f93883cc0c84aae6b16aa4343662b0548777076cab7bffd25364b` | 0x53e0 | `led_device.c`, `application_mode.c`, `jtag_gw.c`, `gpio.c`, `uart.c` |
| `quasar_led_screen.ko` | `10f7ea26a7a1663891c2429148f1675678d414d2ee25a2d1bf3248a52faa6245` | 0x2fb8 | `screen_device.c`, `bootloader_api.c`, `screen_app_api.c` |

Оба собраны `GCC: (Linaro GCC 6.3-2017.02) 6.3.1 20170109`, `-O2 -g`, не stripped:
в `.debug_info` есть полные типы, имена локальных переменных и `.debug_line`.
Это и есть основной источник данных — смещения ниже взяты из `nm`-таблиц
(`.text`-смещения, R_AARCH64 не применены, база 0).

Evidence: `docs/evidence/screen-gowin-artifact.json`,
`docs/evidence/screen-quasar-led-screen-artifact.json` (REA, `inspect-artifact`).

---

## 0. TL;DR

* На этой плате (DT-узел `quasar_led_screen`, `compatible = "ya,led_screen",
  "atri,led-panel"`) биндится **`gowin_led_device`**, а не `quasar_led_screen`
  (у второго `compatible = "quasar_led_screen"`, которого в DT нет).
* Framebuffer `/dev/fb4` — **25x16, 24bpp RGB888**, stride 75, `screen_size`
  = 1200 B (page-aligned). Но **на SPI уходит 8-битная яркость**, 400 байт:
  `pixel = max(R, G, B)`. Никакого RGB по шине не передаётся.
* Транспорт: одиночный SPI (mode 0, 4 МГц, CS по `cs-gpios`), **полный дуплекс**.
  Каждый кадр — две транзакции: (1) команда `SHOW_PIC` 406 B, (2) чтение
  статуса 35 B.
* Кадр уходит в SPI **только по `FBIOPAN_DISPLAY`, `release()` и `destroy()`**
  fbdev. Таймера, workqueue и deferred-IO в драйвере нет. `screend` пишет
  1200 байт через `write()` и закрывает `fd` → кадр уезжает на `release()`.
* Backlight: `max_brightness` из DT (`200`), значение 0..200 уходит как есть
  байтом в `CMD__SET_BRIGHTNESS (0x1B)`, без масштабирования.
* Реконфигурация FPGA — **JTAG-битбэнг** (`TCK` через прямой read-modify-write
  регистра `0xff634468`, бит 7; TDI/TMS/TDO через gpiod), прошивка
  `yandex_led_screen_fpga.bin` (манифест + `crc16`), инструкции Gowin
  `0x11 / 0x15 / 0x71 / 0x75 / 0x3A / 0x3C / 0x02 / 0x41 / 0x13 / 0x09 / 0x05`.
* Прошивка приложения (`yandex-led-screen.bin`) в `quasar_led_screen` заливается
  **по SPI** (bootloader-команды `0xB3/0xBE/0xBC/0xB5`, CRC little-endian,
  ответ с magic `0xFE`). К матрице 25x16 на этой плате это не применяется.

---

## 1. Что реально грузится

DT (вендорский DTS, `tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts:2242`):

```
quasar_led_screen {
    compatible = "ya,led_screen\0atri,led-panel";
    reg = <0x00>;
    spi-max-frequency = <0x3d0900>;        /* 4 MHz */
    max-brightness = <0xc8>;               /* 200 */
    reset-gpios = <0x18 0x4a 0x01>;        /* periphs gpio, ACTIVE_LOW */
    jtag_tck_desc = <0xff634468 0x07>;
    jtag_reconfig-gpios = <0x18 0x19 0x07>;/* ACTIVE_LOW|OPEN_DRAIN */
    jtag_sel-gpios = <0x68 0x0e 0x00>;     /* AO-bank pin 14, ACTIVE_HIGH */
    jtag_tck-gpios = <0x18 0x18 0x00>;
    jtag_tdi-gpios = <0x18 0x15 0x00>;
    jtag_tdo-gpios = <0x18 0x16 0x00>;
    jtag_tms-gpios = <0x18 0x17 0x00>;
};
```

Цепочка биндинга (подтверждена кодом):

1. `of_modalias_node()` срезает префикс вендора: `"ya,led_screen"` → modalias
   `"led_screen"`.
2. `led_device_id` (spi_device_id, `.rodata+0x3a8`) = `{"led_panel", 0}`,
   `{"led_screen", 1}` → `driver_data`.
3. `probe+0x2e28` (`bl spi_get_device_id`), `probe+0x2f90` (`ldr x2,[x23,#32]`) —
   это `driver_data`; 0 => `LED_PANEL`, 1 => **`LED_SCREEN`**.

`of_match_table` (`led_device_of_match`, `.rodata+0x150`) — `ya,led_panel`,
`ya,led_screen`; `quasar_led_screen` имеет `compatible = "quasar_led_screen"` и в
этом DT не встречается — **модуль собирается, но не используется**.

`enum led_device_type { LED_PANEL = 0, LED_SCREEN = 1 }` (DWARF).

Разница вариантов:

| | `LED_PANEL` | `LED_SCREEN` (наша плата) |
|---|---|---|
| fb bpp | 32 (`pixel_size = 4`) | 24 (`pixel_size = 3`) |
| формат пикселя | BGRA in memory (`transp` = биты 24..31) | BGR (R=биты 16..23, G=8..15, B=0..7) |
| кадр на шину | `xres*yres*4` байт, построчный `memcpy` (сырой) | `xres*yres` байт, `max(R,G,B)` |
| разрешение | `rev2res_panel[rev]` | `rev2res_screen[rev]` |
| доп. фичи | touch (`irq-gpios`, `buttons`), power-saving | UART-мост (`has-uart`), gpiochip (`has-gpio`) |

`rev2res_*` — таблица `.rodata+0x4c0` по 2 байта на запись `{u8 xres, u8 yres}`,
индекс — `board_rev` из ответа устройства (0 или 1):

```
+0x4c0: 00 00        panel[0] = невалидно
+0x4c2: 59 01        panel[1] = 89x1     [предположение: 89 колонок, 1 строка]
+0x4c8: 00 00        screen[0] = невалидно
+0x4ca: 19 10        screen[1] = 25x16   <-- наша матрица
```

Отсюда: устройство на этой плате обязано рапортовать `board_rev = 1`, иначе
`probe` падает с `"unsupported board revision: %u"` (`0x1470`).

---

## 2. Схема

```
 userspace (/usr/lib/atrios/screend)
   open("/dev/fb4","wb"); write(1200 B RGB888); close()
        |
        v
 fbdev core: generic fb_write -> screen_base (vmalloc_32_user)
             close() -> fb_release
        |
        v
 gowin_led_device  lp_fb_release / lp_fb_pan_display
        |
        v
   lp_fb_sync(): RGB888 -> 8bpp  pixel = max(R,G,B)  (25*16 = 400 B)
        |
        v
   show_pic_app_cmd(): [07][00][delay_lo][delay_hi][400 B] + CRC16 (BE)  = 406 B
        |
        +--> spi_sync (transfer, 4 MHz)              --- MOSI -->
        |
        +--> get_lp_status(): 35 B read (tx_buf = NULL)  <-- MISO --
                 |  CRC16(buf,35) == 0 ?
                 |  last_cmd_status & (CRC|CMD_OVF) -> повтор
                 v
              OK / dev_err
        |
        v
   backlight (class backlight) -> CMD__SET_BRIGHTNESS (0x1B)
```

---

## 3. `gowin_led_device`: устройство и probe

### 3.1 `struct lp_driver` (DWARF, `sizeof` = 336 = 0x150)

| off | поле | примечание |
|---|---|---|
| 0 | `spi_device *spi` | |
| 8 | `jtag_gpios *jtag_gpios` | |
| 16 | `fb_info *fb` | |
| 24 | `input_dev *input` | touch |
| 32 | `gpio_chip *gpio_chip` | экспандер `led_panel` (только `has-gpio`) |
| 40 | `u8 cached_gpio` | |
| 48 | `mutex cached_gpio_lock` | |
| 88 | `tty_driver *uart_driver` | UART-мост |
| 96 | `tty_port *uart_port` | |
| 104 | `timer_list uart_poll_timer` | |
| 176 | `work_struct uart_rx_work` | |
| 208 | `int uart_mosi_bytes`, 212 `uart_miso_bytes` | статистика моста |
| 216 | `fw_update_info *fw_update_info` | |
| 224 | `u32 bi2ke[7]` | кнопки из DT |
| 256 | `int buttons_count`, 260 `open_fb_count`, 264 `irq` | |
| 272 | `work_struct touch_work`, 304 `workqueue_struct *touch_wq` | |
| 312 | `led_device_type device_type` | |
| 316 | `u8 brightness`, 317 `u8 max_brightness` | |
| 320 | `u32 crc_err` | счётчик локальных CRC-ошибок |
| 324 | `atomic_t next_frame_delay` | мс, sysfs RW |
| 328 | `u32 frame_size` | размер кадра на шину |
| 332 | `u8 frame[]` | буфер кадра |

`struct jtag_gpios` (88 B): `dev(0) reconfig(8) sel(16) tck(24) tdi(32) tdo(40)
tms(48) tck_addr(56) tck_mask(64) reset(72) gpio_mode(80)`.

`gpio_mode`: `0` = JTAG, `1` = SPI (см. `select_jtag_mode`/`select_spi_mode`).

### 3.2 probe (`0x2e00`) — порядок действий

```
_dev_info("binding to %s")
jtag = kzalloc(88); jtag->dev = &spi->dev; jtag->gpio_mode = 1   /* SPI */
of_property_read_u32_array(np,"jtag_tck_desc",2)   -> addr, bit
jtag->tck_addr = ioremap(addr, 32)
jtag->tck_mask = 1 << bit                          /* 1 << 7 */
jtag->reset    = devm_gpiod_get(dev,"reset",    GPIOD_OUT_LOW)
jtag->reconfig = devm_gpiod_get(dev,"jtag_reconfig", GPIOD_OUT_LOW)
msleep(300)
jtag->sel      = devm_gpiod_get(dev,"jtag_sel", GPIOD_OUT_HIGH)   /* = SPI mode */
jtag->tms      = of_get_named_gpio_flags(np,"jtag_tms-gpios") + gpiod_direction_output(0)
fw_update = kzalloc(56); fw_update->{manifest,bin}_name = "yandex_led_screen_fpga.bin[.manifest]"
status = lp_handle_firmware_update(spi, jtag, fw_update)   /* см. §7 */
if (status == UPD_STATUS__ERROR) return 0;                 /* !!! без framebuffer */
get_status_app_cmd(spi, &resp)                             /* 35 B, см. §4 */
_dev_info("board_rev=%hhu, fw_version=%hhu", resp.board_rev, resp.fw_version)
res = get_screen_resolution(resp.board_rev)                /* 25x16 */
fb = framebuffer_alloc(0x150 + xres*yres*pixel_size, dev)
...
register_framebuffer()
backlight_device_register(name=fix.id, &fb, &lp_bl_ops, NULL)
dev_set_drvdata(&spi->dev, fb)
sysfs_create_groups()
init_touch()          /* необязателен: devm_gpiod_get_optional(dev,"irq",IN) */
if (of_find_property(np,"has-uart")) lp_uart_init()
if (of_find_property(np,"has-gpio")) gpio_init()
```

Важная деталь: `GPIOD_OUT_LOW`/`GPIOD_OUT_HIGH` — это **логические** уровни; для
`reset` в DT стоит `GPIO_ACTIVE_LOW`, т.е. в probe линия отпущена (физически
высокий), а `fpga_hw_reset()` (`0x24c8`) даёт импульс сброса:

```
gpiod_set_value(reset, 1)   /* active-low => физически низкий = сброс */
msleep(10)
gpiod_set_value(reset, 0)
msleep(300)
_dev_info("The FPGA was reset")
```

### 3.3 Framebuffer

`fb_var_screeninfo` (смещения от `fb_info+0x60`, размер 160):

| поле | значение |
|---|---|
| xres / yres | 25 / 16 (из `rev2res_screen`, не из устройства!) |
| xres_virtual / yres_virtual | 25 / 16 |
| bits_per_pixel | 24 (`pixel_size*8`) |
| grayscale | 0 |
| red | offset 16, length 8 |
| green | offset 8, length 8 |
| blue | offset 0, length 8 |

`fb_fix_screeninfo` (`fb_info+0x100`): `id = "YNDX LED screen"`,
`visual = FB_VISUAL_TRUECOLOR (2)`, `line_length = xres*3 = 75`,
`ypanstep = yres`,
`smem_len = screen_size = ALIGN(yres*line_length, 4096) & 0xFF000` (1200 → 4096),
`screen_base = vmalloc_32_user(screen_size)`.

`lp_fb_check_var` (`0x1768`) запрещает менять `xres`/`yres`
(`"attempts to change panel resolution"`, `-EINVAL`) и проверяет, что
`xres_virtual*yres_virtual*bpp/8 <= screen_size`
(`"resize virtual panel out of video memory"`, `-ENOMEM`).
`lp_fb_set_par` (`0x7d8`) только пересчитывает `line_length`.

`fb_ops` (`.data+0x50`): `owner`, `fb_open = 0x818` (инкремент `open_fb_count`),
`fb_release = 0x1dd0` (**декремент + `lp_fb_sync`**), `fb_check_var = 0x1768`,
`fb_set_par = 0x7d8`, `fb_pan_display = 0x1da0` (**`lp_fb_sync`**),
`fb_mmap = 0x1b60`, `fb_sync = 0x1b98`, `fb_destroy = 0x21f8`
(`memset(screen_base,0)` + `lp_fb_sync` + `vfree`).
`fb_fillrect`/`copyarea`/`imageblit`/`fb_write` — **нет**.

`fb_mmap` = `remap_vmalloc_range(vma, screen_base, 0)`.

### 3.4 Конвертация кадра (`lp_fb_sync`, `0x1b98`)

```
if (device_type == LED_PANEL) {                 /* 0x1cf0 */
    for (y = yoffset; y < yoffset+yres; y++)
        for (x = xoffset; x < xoffset+xres; x++)
            memcpy(dst, screen_base + y*line_length + x*bytes_pp, bytes_pp);
} else if (device_type == LED_SCREEN) {         /* 0x1bd4 */
    for (y = yoffset; y < yoffset+yres; y++)
        for (x = xoffset; x < xoffset+xres; x++) {
            u32 p = *(u32 *)(screen_base + y*line_length + x*3);
            u8 b =  p        & 0xff;
            u8 g = (p >>  8) & 0xff;
            u8 r = (p >> 16) & 0xff;
            *dst++ = max(r, max(g, b));
        }
} else return -EBADR(? -121);
show_pic_app_cmd(spi, driver->frame /*+332*/, driver->frame_size /*+328*/,
                 driver->next_frame_delay /*+324*/);
```

Ассемблерная суть (0x1c3c..0x1c64):

```
ldr  w1,[x0,w3]      ; u32 из fb
and  w3,w1,#0xff     ; blue  (младший байт = B, т.к. blue.offset = 0)
lsr  w0,w1,#16 / and -> w5 = red
lsr  w1,w1,#8  / and -> w1 = green
csel w0,w1,w0,ls     ; w0 = (red <= green) ? green : red      = max(r,g)
cmp  w3,w0
csel w0,w0,w3,cc     ; w0 = (blue < max) ? max : blue         = max(r,g,b)
strb w0,[x4],#1
```

Итог: **1 байт на пиксель, 400 байт на кадр**, порядок — построчно, слева
направо, сверху вниз, без упаковки/перестановок. Это же подтверждает userspace:
`usr/lib/atrios/screend` пишет 1200 байт RGB888 и полагается на «Driver computes
brightness = max(R, G, B)», причём заливает `val` во все три канала.

`frame_size` считается в `probe` (`0x30f0`):

```
x27 = xres*yres;  if (device_type != LED_PANEL) x27 = xres*yres;   /* == 400 */
                  else                         x27 = xres*yres*pixel_size;
```

(в коде это `csel x27,x27,x0,ne`, где `x0 = xres*yres*pixel_size`.)

Размер `par` при этом всегда `0x150 + xres*yres*pixel_size`, т.е. для экрана
выделяется 336+1200 байт, но используется 400 — небольшой перерасход.

### 3.5 Механизм обновления

Ни таймера, ни workqueue, ни `fb_deferred_io` у кадров нет (`timer_list` в
`lp_driver` — только для UART-моста). Кадр отправляется **только**:

* `FBIOPAN_DISPLAY` → `fb_pan_display` → `lp_fb_sync`;
* `close(/dev/fb4)` → `fb_release` → `lp_fb_sync`;
* `fb_destroy` (при выгрузке/ошибке регистрации).

`write()` в `/dev/fb4` идёт generic-путём fbdev (`memcpy` в `screen_base`) и
синка не вызывает. Поэтому контракт userspace: «записать 1200 байт и закрыть
fd» (или mmap + `FBIOPAN_DISPLAY`). Именно так делает `screend`.

`next_frame_delay` (sysfs, `kstrtou16`, мс) уходит в пакет `SHOW_PIC`; в
fpga-очередь кадров (`frames_in_queue`) драйвер сам не пушит — очередь
организует только FPGA, если приложение присылает кадры чаще, чем `delay`.

### 3.6 Backlight, hwmon, sysfs

* `lp_bl_update_status` (`0x1b00`): `driver->brightness = bl->props.brightness`
  (байт!), затем `set_brightness_app_cmd(spi, brightness)` под `mutex` —
  `[1B][value][crc16 BE]`, 4 байта. `max_brightness` = DT `max-brightness`
  (200, clamp 255), начальная яркость = `max_brightness`.
* `lp_bl_get_brightness` возвращает кэш, `lp_bl_check_fb` — `fb == driver->fb`.
* hwmon в `gowin_led_device` **нет** (в отличие от `quasar_led_screen`, где
  `hwmon` отдаёт `temp_celsius`).
* sysfs-группы: `jtag` (`test_fpga_prog`, `jtag_fpga_prog`, `jtag_erase_flash`,
  `jtag_codes`, `jtag_reconfig`, `gpio_mode`), `lp_common` (`open_fb_count`,
  `crc_errors`, `fw_upd_status`, `power_saving_mode`, `set_animation_mode`,
  `frame_queue_overflows`, `help`), `touch` (`show_debug_info`, `press_key`),
  `frame_queue` (`next_frame_delay`, `frames_in_queue`).

Полный self-documenting текст — строка `0x0160` в `.rodata.str1.8`.

---

## 4. SPI-протокол приложения (`application_mode.c`)

### 4.1 Транспорт

* SPI mode 0 (DT без `spi-cpol`/`spi-cpha`), 8 бит/слово,
  `spi-max-frequency = 4 MHz` (`0x3d0900`). Все передачи драйвера идут на
  дефолтной скорости (нигде не выставляется `transfer.speed_hz`).
* CS — `cs-gpios = <GPIOH_6>` в узле `spi@15000`; `num-cs = 1`.
* Полный дуплекс. `struct spi_transfer` собирается вручную
  (`spi_message_init`/`spi_message_add_tail`).

Две операции:

| Функция | tx | rx | смысл |
|---|---|---|---|
| `lp_write` (`0x4a88`) | `size` байт команды (последние 2 — CRC) | — | отправить команду |
| `get_lp_status` (`0x4598`) | `NULL` (MOSI = 0) | 35 байт | прочитать статус |

`lp_write` **после** команды всегда вызывает `get_lp_status`, т.е. на одну
команду приходится **две** SPI-транзакции (с запретом прерываний вокруг
JTAG-операций это не связано — там отдельно). Команда «сама по себе» не
подтверждается: результат читается отдельным запросом статуса.

### 4.2 Формат команды

```
+--------+--------+--------+ ... +--------+--------+--------+
| cmd    | param  | (payload, если есть)      | CRC16 hi| lo   |
+--------+--------+--------+ ... +--------+--------+--------+
                                        ^ over 0..size-3
```

CRC: `crc_itu_t(0, buf, size-2)` (CRC-16/ITU-T, полином 0x1021, init 0),
упакован **big-endian**:

```
4ad8: bl  crc_itu_t            ; crc_itu_t(0, buf, size-2)
4adc: rev16 w0,w0
4ae0: strh w0,[x23,x19]        ; buf[size-2] = hi, buf[size-1] = lo
```

`size` в `lp_write` — полный размер с CRC (`sub x19,x2,#0x2`).

Примеры (все — из дизассемблера, точные байты):

| команда | пакет | размер |
|---|---|---|
| `SHOW_PIC` | `07 00 <delay_lo> <delay_hi> <400 B кадра> <crc hi> <crc lo>` | 406 |
| `SET_BRIGHTNESS` | `1B <brightness> <crc hi> <crc lo>` | 4 |
| `SET_ANIMATION` | `24 <mode> 00*15 <crc hi> <crc lo>` | 19 |
| `SET_POWER_SETTING_MODE` | `EC <mode> <crc hi> <crc lo>` | 4 |
| `RESET_TOUCH_STATE` | `C1 00 <crc hi> <crc lo>` | 4 |
| `SET_GPIO` | `56 <gpio> <crc hi> <crc lo>` | 4 |
| `UART_FLUSH_DATA` | `FD 00 00 00 00 <crc hi> <crc lo>` | 6 |
| `UART_SEND_DATA` | `5D <data...> <crc hi> <crc lo>` (data ≤ 1024) | len+3 |
| `UART_GET_DATA` | `6D ... <crc>` (только `has-uart`) | 67 |
| `GET_STATUS` | — (только чтение 35 B) | 0 |

Замечание: `delay` в `SHOW_PIC` стоит **перед** картинкой (байты 2..3, u16
little-endian), а не после, как в `quasar_led_screen`. Байт 1 у всех команд,
кроме `SET_BRIGHTNESS`, — 0 (назначение неизвестно; `[предположение]`
резерв/флаги).

`enum pa_command` (DWARF, имена вендора):

```
CMD__GET_STATUS               = 0
CMD__SHOW_PIC                 = 7
CMD__SET_BRIGHTNESS           = 27   (0x1B)
CMD__SET_ANIMATION            = 36   (0x24)
CMD__SET_GPIO                 = 86   (0x56)
CMD__UART_SEND_DATA           = 93   (0x5D)
CMD__UART_GET_DATA            = 109  (0x6D)
CMD__RESET_TOUCH_STATE        = 193  (0xC1)
CMD__SET_POWER_SETTING_MODE   = 236  (0xEC)
CMD__SET_TOUCH_POLLING_PERIOD = 240  (0xF0)   /* в драйвере не используется */
CMD__UART_FLUSH_DATA          = 253  (0xFD)
```

`enum pa_animation_mode { STOP=0, BOOT_ANIMATION=1 }`,
`enum power_saving_mode { OFF=0, ON=1 }`,
`enum single_ended_mode { PUSH_PULL=0, OPEN_DRAIN=1, OPEN_SOURCE=2 }`.

### 4.3 Формат ответа (35 байт)

`get_lp_status` читает 35 байт в буфер и **пропускает байт 0** (первый
принятый байт не используется):

| байт | поле | тип |
|---|---|---|
| 0 | (не читается драйвером) | — |
| 1 | `mode` (версия приложения / «application mode») | u8 |
| 2 | `fw_version` | u8 |
| 3..4 | `board_rev` | u16 LE |
| 5..6 | `uf_crc` (CRC user-flash, т.е. битстрима в flash FPGA) | u16 LE |
| 7..10 | `frame_overflows` | u32 LE |
| 11..14 | `crc_errors` (счётчик ошибок CRC на стороне FPGA) | u32 LE |
| 15 | `frames_in_queue` | u8 |
| 16 | `last_cmd_status` | u8 (битовая маска) |
| 17 | `device_mode` | u8 |
| 18.. | panel: `touch_state`, `touch_0/2/4_dbg_val`, `touch_0/2/4_metric`, `touch_mask` | |
| 18.. | screen: `uart_miso_bytes` (u16), `uart_mosi_bytes` (u16) | |
| 33..34 | CRC16 | BE, `crc_itu_t(0, buf, 35) == 0` |

Проверка CRC — «остаточная»: считается CRC по всем 35 байтам, включая поле CRC,
и сравнивается с нулём (`0x46e4..0x46ec`). Это эквивалент
`crc_itu_t(0, buf, 33) == be16(buf[33..34])` и подтверждает порядок big-endian
для CRC ответа (в отличие от bootloader-протокола, см. §5.3).

Смещение байта 1 для `mode` — не ошибка чтения: `struct pa_response`
(host-side, 36 B) — это уже переупакованная структура; драйвер раскладывает
поля вручную (`0x466c..0x46b0` + `0x46c0`/`0x4870`), причём для `LED_SCREEN` и
`LED_PANEL` раскладка «хвоста» разная. `[проверить на железе]` — что именно
находится в байте 0 (dummy-байт полнодуплексной передачи или sync/длина).

`enum pa_cmd_status` (битовая маска `last_cmd_status`):

| бит | значение | реакция `lp_write` |
|---|---|---|
| 0 | `COMMAND_ERROR` | `dev_err("CMD_ERROR")` → `-121` |
| 1 | `CRC_ERROR` | **retry** |
| 2 | `CMD_OVF_ERROR` | `usleep_range(10,20)` → **retry** |
| 3 | `FRM_OVF_ERROR` | только предупреждение, ошибкой **не считается** (маска `& ~8`) |
| 4 | `SPKT_ERROR` | `dev_err` → `-121` |
| 5 | `LPKT_ERROR` | `dev_err` → `-121` |
| 6 | `UART_MOSI_OVF` | `dev_err` → `-121` |

`lp_write` (`0x4b58..0x4bb8`):

```
for (i = 0; i < retries; i++) {
    spi_sync(cmd);                      /* 1-я транзакция */
    if (get_lp_status(spi, &resp, retries)) return err;
    st = resp.last_cmd_status;
    if (st & CMD_OVF_ERROR) { usleep_range(10, 20); continue; }
    if (!(st & CRC_ERROR)) break;
}
if (st) { /* разбор битов, маска ~FRM_OVF_ERROR */ }
```

Число попыток по командам: `SHOW_PIC` — **1**, `SET_BRIGHTNESS` — 5,
`SET_ANIMATION` — 5, `UART_*` — 5, `GET_STATUS` — 5 (`get_status_app_cmd`
передаёт 5 внутрь `get_lp_status`).

### 4.4 `get_lp_status` (`0x4598`)

```
for (i = 0; i < retries; i++) {
    spi_sync(NULL-tx, rx=buf, 35);        /* чистый read */
    if (err) { dev_err("spi transfer failed: %d"); return err; }
    ... разложить поля ...
    if (crc_itu_t(0, buf, 35) == 0) break;
    dev_warn("response crc mismatch"); print_hex_dump("response raw data: ", buf, 35);
    ... dev_warn по каждому полю ("led panel response:") ...
    driver->crc_err++;
    usleep_range(10, 20);
}
if (buf[1] != 1) { dev_err("application mode number mismatch: ..."); return -EPROTO; }
if (driver && device_type == LED_PANEL) { driver->uart_mosi_bytes = ...; }
```

Т.е. «приложение живо» проверяется по `mode == 1` — вот почему в
`lp_handle_firmware_update` после перепрошивки ждут именно `mode` (см. §7).

Битые ответы логируются `print_hex_dump` (KERN_DEBUG level, groupsize 16,
ASCII), плюс отдельные `dev_warn` по полям — удобно для первых запусков на
mainline (можно один в один повторить для отладки).

---

## 5. `quasar_led_screen.ko` — «второй» вариант протокола

Не биндится на этой плате, но полезен: он новее (API v9), умеет CRC-режим,
обновление прошивки приложения через SPI и hwmon-температуру. Если
когда-нибудь менять прошивку FPGA/приложения — понадобится именно он.

### 5.1 Роли

`screen_device.c` (fbdev/backlight/hwmon/update) + `bootloader_api.c`
(загрузчик прошивки приложения) + `screen_app_api.c` (протокол приложения).

Framebuffer идентичен по геометрии (`25x16`, `24bpp`, BGR, `line_length = 75`,
`id = "YNDX LED screen"`), **но** `var.grayscale = 1` (`probe+0x12c`,
смещение `var+0x7C`) — в отличие от `gowin_led_device`, где 0.
Разрешение берётся **из ответа устройства** (`sa_info.xres/yres`), а не из
таблицы; размер `par` = `0x30 + xres*yres`, если `pixel_depth > 1`.

Конверсия кадра (`qls_fb_sync`, `0x858`) — та же самая `max(R,G,B)` в 1 байт,
но реализована общим циклом по битфилдам (`v*255/mask` с `udiv`), т.е. медленнее.

### 5.2 Протокол приложения v9

`enum SA_COMMAND`: `NONE 0, TEST 4, START_BOOT_ANIMATION 36, SHOW_PIC 7,
FLUSH_BUFFER 45, SET_BRIGHTNESS 27, SAVE_TEST_SETTINGS 9, SET_CRC_CHECK 54,
REBOOT 11`.

`sa_write_v9` (`0x23d0`):

```
crc = crc_itu_t(0, buf, size); buf[size..size+1] = crc (BE)
spi_sync(tx=buf, len=size+2)            /* только TX */
sa_none_unsafe(spi, rx, 20)             /* 22 байта 0x00 + CRC, полный дуплекс */
sa_parse_response(rx, 20, &resp)
return resp.last_cmd_status ? -121 : 0
```

Формат пакета `SHOW_PIC` (v9) — **другой**, чем в gowin:

```
07 <image xres*yres байт> <delay u16 LE> <crc hi> <crc lo>
```

`enum SA_CMD_STATUS { OK = 0, CRC_ERROR = 21 }`;
`struct sa_info { api_version, xres, yres, pixel_depth, frame_queue_overflows,
frames_in_frame_queue, temp_celsius, fw_version, last_cmd_status }` (12 B);
ответ 20 B, CRC сравнивается как u16 little-endian (`sa_parse_response`) —
внутренняя несогласованность с BE-CRC команды, `[проверить на железе]` при
необходимости.

Поддержка версий — таблица `fw_versions` (`.rodata+0x308`, 10 записей по 16 B,
`struct sa_fw_impl { bool supported; bool has_separate_fw_version;
bool has_crc_support; struct sa_ops ops; }`):

| api | supported | separate fw_version | crc | write-op |
|---|---|---|---|---|
| 0..4 | 0 | — | — | — |
| 5 | 1 | 0 | 0 | `spi_write` (0x2010, без CRC) |
| 6 | 1 | 0 | 0 | `spi_write` |
| 7 | 1 | 1 | 0 | `spi_write` |
| 8 | 0 | — | — | — |
| 9 | 1 | 1 | 1 | `sa_write_v9` |

`devm_sa_device_init` (`0x2810`) — «рукопожатие»:

```
10 раз: sa_none_unsafe(20 B) + sa_parse_response;
        если -121 (CRC) -> dev_warn("response crc mismatch with api version %hhu") + ~100 ms
после 10 неудач: если api_version > 9 или !supported или has_crc_support -> -121
если api_version == 0 ("response is empty"):
        gpiod_direction_output(reset, 0); msleep(10);
        gpiod_set_value(reset, 1);        msleep(2000);   /* сброс + 2 c */
        повторить рукопожатие
иначе: devm_kmalloc(65056) -> sa_device{spi, fw_impl, data, gpio_reset}
```

`sa_device` = 65056 B (в основном `u8 cmd_buf[65029]`). `frame_queue_overflows`
инкрементируется в каждом `sa_none`.

### 5.3 Bootloader-протокол (обновление прошивки приложения по SPI)

`bl_response` (28 B, но разбираются первые 14): `magic(0) version(1)
fw_status(2) state(3) reserved[8](4..11) crc16(12..13)`, `magic = 0xFE`.
CRC — **little-endian**, `crc_itu_t(0, buf, 12) == le16(buf[12..13])`
(`bl_get_info`, `0x1648`).

`enum BL_COMMAND { WRITE=179(0xB3), RESTART=190(0xBE), CHECK_AND_RUN=188(0xBC),
RUN_APP=181(0xB5) }`, `enum BL_STATE { IDLE=0, FAIL=1, BUSY=3, BAD_PARAMETER=7,
CRC_ERROR=21 }`, `enum APP_FW_STATUS { OK=0, NOT_OK=1 }`.

| команда | пакет | размер | примечание |
|---|---|---|---|
| `WRITE` | `B3 <chunk> <crc lo> <crc hi>` | chunk+3 | `bl_write_app_fw` бьёт образ на куски; чанк ≤ 32 KiB (`cmp size,#0x8000`) |
| `RESTART` | `BE <crc lo> <crc hi>` | 3 | сброс указателя записи |
| `CHECK_AND_RUN` | `BC <crс16 образа lo> <hi> <crc lo> <hi>` | 5 | |
| `RUN_APP` | `B5 <crc lo> <crc hi>` | 3 | |
| (опрос) | `bl_none`: tx=NULL, rx=28 B, **udelay 500 ms** перед передачей, `speed_hz = 1 MHz` | 28 | |

Все BL-транзакции идут на **1 МГц** (`speed_hz = 0xF4240`, `0x1868`), в отличие
от приложения (4 МГц).

`bl_wait_until_ready(spi, bl, retries, delay)` — `retries × (bl_get_info +
udelay(delay*10 ms))`, `-ETIMEDOUT` по исчерпании; в `probe` вызывается как
`(10, 100)`, т.е. до ~10 с.

Флоу обновления (`probe`, `0x1000`-е адреса):

```
прочитать sa_info -> current fw_version/api_version
request_firmware("yandex-led-screen.bin.manifest") -> sscanf:
    "min_api_version = %hhu\nmax_api_version = %hhu\nfw_version = %hhu\n
     crc16 = %hu\nforce_downgrade = %hhu\n"
request_firmware("yandex-led-screen.bin") -> crc_itu_t(0,data,size) == manifest.crc16
если нужен апдейт:
    sa_reboot(0x0B)                       /* уйти в бутлоадер */
    bl_wait_until_ready(10, 100)
    bl_get_info  -> "bootloader version=%hhu, fw_status=%s[%02x], state=%s[%02x]"
    bl_write_app_fw(data,size)            /* WRITE чанками */
    bl_check_and_run(crc16)
    ждать до 100 × 100 ms: sa_fw_version_unsafe() == новый api_version
    "trying to reenable crc mode" -> sa_set_crc_check(sa, true)
```

Обновление прошивки **FPGA** (`yandex_led_screen_fpga.bin`) — только по JTAG, в
`gowin_led_device` (§6, §7).

---

## 6. JTAG: реконфигурация Gowin

### 6.1 Линии

| сигнал | DT | способ доступа |
|---|---|---|
| `reset` | periphs pin 0x4a, ACTIVE_LOW | gpiod, OUT_LOW (отпущен), импульс в `fpga_hw_reset` |
| `jtag_reconfig` | periphs pin 0x19, ACTIVE_LOW\|OPEN_DRAIN | gpiod, OUT_LOW (неактивен) |
| `jtag_sel` | AO-bank pin 0x0e, ACTIVE_HIGH | gpiod, OUT_HIGH == SPI-режим, `set 0` == JTAG |
| `jtag_tck` | periphs pin 0x18 (**GPIOH_7**) | gpiod **только для захвата пина**; тактирование — прямой RMW регистра `0xff634468`, бит 7 (`jtag_tck_desc`) |
| `jtag_tdi` | periphs pin 0x15 (GPIOH_4) | gpiod |
| `jtag_tdo` | periphs pin 0x16 (GPIOH_5) | gpiod, вход |
| `jtag_tms` | periphs pin 0x17 (GPIOH_6) | `of_get_named_gpio_flags` + `gpiod_direction_output(0)` |

> Расхождение: в задании `sel = GPIOE_2`, в DTB — **AO-bank (phandle 0x68,
> `ao-bank@ff800014`) пин 14**. Нужно перепроверить на схеме/железе.

Порядок `devm_gpiod_get` в probe: `reset` (OUT_LOW) → `jtag_reconfig` (OUT_LOW)
→ `msleep(300)` → `jtag_sel` (OUT_HIGH = SPI) → `jtag_tms` (=0).

### 6.2 Примитивы

`jtag_write(jtag, u8 data, u8 tms)` (`0x36c0`):

```
gpiod_set_value(tms, 0)
for (i = 0; i < 8; i++) {          /* LSB first */
    gpiod_set_value(tdi, (data >> i) & 1);
    if (i == 7) gpiod_set_value(tms, tms & 1);
    TCK_low();  busy(100);          /* RMW 0xff634468, бит 7 */
    TCK_high(); busy(100);
}
```

`jtag_update_tms(jtag, tms)` (`0x3798`) = `set TMS; TCK_low; busy; TCK_high; busy`.

`TCK_low/TCK_high` (`0x3708..0x3760`) — это

```
w2 = readl(tck_addr);  dsb ld; dsb st;
writel((w2 & ~mask), tck_addr);        /* 0 */
for (i = 0; i < 100; i++);             /* ~суб-µs */
w2 = readl(tck_addr);
writel(((mask & ~w2) ^ w2), tck_addr); /* 1 */
for (i = 0; i < 100; i++);
```

Никаких `ndelay`; задержка — пустой цикл.

### 6.3 TAP

`jtag_tap_move(jtag, from, to)` (`0x3828`), `enum tap_states` `[предположение:
значения по IEEE1149.1 подряд с 0]`:

```
1  = RUN_TEST_IDLE, 4 = SHIFT_DR, 5 = EXIT1_DR,
11 = SHIFT_IR, 12 = EXIT1_IR, 16 = «состояние неизвестно» (сброс TAP)
```

| from → to | последовательность TMS |
|---|---|
| `16 → 1` | 8×1, 0, 0 (принудительный Test-Logic-Reset) |
| `1 → 1` | 0, 0 (idle) |
| `1 → 11` | 1, 1, 0, 0 (Select-DR, Select-IR, Capture-IR, Shift-IR) |
| `1 → 4` | 1, 0, 0 (Select-DR, Capture-DR, Shift-DR) |
| `12 → 1` | 1, 0, 0 (Update-IR, RTI, RTI) |
| `5 → 1` | 1, 0, 0 (Update-DR, RTI, RTI) |
| иначе | `dev_err("Unknown TAP move.")` |

`jtag_write_inst(jtag, u8 inst)` (`0x3940`): `move(1,11)` → `write(inst, tms=1)`
→ `move(12,1)` → 3× `move(1,1)` (6 idle-тактов).

`jtag_read_code(jtag, u8 inst)` (`0x3b10`): `write_inst(inst)` → `move(1,4)` →
32 такта с `TMS=0` (на 32-м `TMS=1`), TDO сэмплируется **после** фронта TCK,
биты складываются LSB-first → `move(5,1)` → возвращает `u32`.

### 6.4 Инструкции и последовательности

Значения инструкций Gowin (имена не документированы, ниже — по месту вызова):

| опкод | где | смысл (по логам) |
|---|---|---|
| `0x11` | везде первым | чтение IDCODE/переход в конфиг-режим |
| `0x13` | `jtag_prog_fpga` финал | чтение `USER CODE` |
| `0x15` | erase/prog | «no-op»/подготовка конфигурации |
| `0x41` | erase/prog | чтение **STATUS CODE** (см. ниже) |
| `0x71` | prog | запись строки конфигурации |
| `0x75` | erase SRAM | стирание SRAM |
| `0x3A`, `0x3C`, `0x02` | erase | стирание/завершение операций с flash |
| `0x09`, `0x05` | erase (SRAM-ветка) | |

**STATUS CODE** (`check_status_code`, `0x39c8`), читается инструкцией `0x41`:

| бит | смысл (строка) | норма |
|---|---|---|
| 0 | `Status Code: CRC Error` | 0 |
| 1 | `Status Code: Bad Command` | 0 |
| 2 | `Status Code: ID Verify failed` | 0 |
| 3 | `Status Code: Timeout` | 0 |
| 12 | `Status Code: VLD bit isn't set` | 1 |
| 15 | `Status Code: READY bit isn't set` | 1 |
| 16 | `Status Code: POR bit isn't set` | 1 |

После разбора кода: `(code & ~0x20000) == 0x19020` — иначе `-EPROTO`.
(бит 17 игнорируется; ожидаемое значение — биты 5, 12, 15, 16.)

**`jtag_erase_flash(jtag)` (`0x3c20`)**:

```
code = read_code(0x41)
if (check_status_code(code) != 0) {                 /* SRAM «грязная» */
    info("Start erasing the FPGA SRAM"); info("    status code = 0x%x", code)
    write_inst(0x11); write_inst(0x15); write_inst(0x75)
    6 × TMS=0;  move(1,4); 6 × TMS=0
    TAP-reset-паттерн: 16 × (TMS=1, TMS=0); TMS=1,0,1,1,0,0,0,0
    320000 тактов TCK (0x4E200)
    write_inst(0x3A); write_inst(0x02)
    local_irq_enable(); msleep(200)
    write_inst(0x11); write_inst(0x3C); write_inst(0x02); msleep(200)
    code = read_code(0x41);
    if (bad) { error("Erasing the FPGA SRAM was failed"); return -EPROTO }
    info("Erasing the FPGA SRAM was successful")
} else info("FPGA SRAM doesn't need to be erased")

info("Start erasing the FPGA FLASH")
local_irq_save(); irq_trace_start()
write_inst(0x11); write_inst(0x15); write_inst(0x75)
6 × TMS=0; move(1,4); 6 × TMS=0
16 × (TMS=1, TMS=0); TMS=1,0,1,1,0,0,0,0
320000 тактов TCK
write_inst(0x3A); write_inst(0x02)
irq_trace_stop(); local_irq_enable(); msleep(200)
write_inst(0x11); write_inst(0x3C); write_inst(0x02); msleep(200)
code = read_code(0x41); if (bad) { error("Erasing the FPGA FLASH was failed"); return -EPROTO }
info("Erasing the FPGA FLASH was successful")
```

`irq_trace_start/stop` — вендорские хуки Amlogic (`/proc/trace`), в mainline
их нет; важна только защита от прерываний на время битбэнга.

**`jtag_prog_fpga(jtag, firmware *fw, u32 user_code)` (`0x3fe0`)**:

```
info("Start FPGA programming")
size = ALIGN(fw->size, 256)                       /* добивка 0xFF */
buf = kmalloc(size); memset(buf, 0xFF, size); memcpy(buf, fw->data, fw->size)
buf[0..3] = "GW1N"                                /* 47 57 31 4E */
info("fw size = %lu", size)
info("FPGA ID = %x", read_code(0x11))
write_inst(0x15); 4000 × TMS=0
for (row = 0; row < size/256; row++) {
    write_inst(0x15); write_inst(0x71)
    if (row == 0) { move(6,4); 6 тактов TDI=0 }
    else          { 16 тактов TDI=0 }
    /* 26-битный адрес строки, LSB-first, TMS=1 на 26-м бите */
    move(5,1)
    360 тактов TCK
    /* 256 байт: по 4 байта, байты внутри слова переставляются, jtag_write(x, tms=1 на последнем) */
    24 такта TCK
}
write_inst(0x3A); write_inst(0x02); 300 × TMS=0
write_inst(0x11); write_inst(0x3C); write_inst(0x02); msleep(300)
kfree(buf)
uc = read_code(0x13)                              /* USER CODE */
if (uc != user_code) { error("user_code: expected = %u; actual = %u"); return -EPROTO }
info("FPGA has been successfully programmed")
```

Т.е. образ в `yandex_led_screen_fpga.bin` — **уже** Gowin-«строками» по 256
байт, в которых первые 4 байта каждой строки заменяются на `"GW1N"`, а сам
драйвер только переставляет байты внутри 4-байтовых групп (bit/byte-reverse,
`0x4298..0x42f8`). Это соответствует «сырому» формату `.fs`, а не `.bin` от
Gowin Programmer — `[проверить на железе]` сравнением с образом из прошивки.

### 6.5 Переключение SPI ↔ JTAG

`select_jtag_mode(spi, jtag)` (`0x10d0`):

```
if (gpio_mode == 0) { info("jtag mode is already set"); return 0; }
gpiod_set_value(jtag->sel, 0)                  /* переключить мультиплексор на FPGA */
pinctrl = devm_pinctrl_get(&spi->master->dev)  /* pinctrl SPI-контроллера */
... отпустить ссылки (цикл devres_pinctrl_put) ...
jtag->tck = devm_gpiod_get(dev, "jtag_tck", OUT_LOW)
jtag->tdi = devm_gpiod_get(dev, "jtag_tdi", OUT_LOW)
jtag->tdo = devm_gpiod_get(dev, "jtag_tdo", IN)
jtag->gpio_mode = 0
jtag_read_code(jtag, 0x11)                     /* «пинг» TAP */
```

`select_spi_mode(spi, jtag)` (`0x13f8`) — обратное: `sel = 1`, `gpiod_put`
для tck/tdi/tdo, `devm_pinctrl_get(&spi->master->dev)` +
`pinctrl_lookup_state("default")` + `pinctrl_select_state` (возврат SPI-пинов),
`gpio_mode = 1`, затем `get_status_app_cmd()` для проверки, что приложение живо.

Захват gpio-дескрипторов сам по себе переключает pinmux в GPIO-функцию (это
делает AML pinctrl при `gpiod_request`), поэтому для JTAG отдельное состояние
pinctrl не нужно — нужен только «отпуск» состояния SPI-контроллера.

---

## 7. Обновление FPGA-прошивки (фреймворк)

`fw_update_info` (56 B): `fw_version(0) crc16(2) uf_crc16(4) user_code(8)
force_upd(12) firmware *fw(16) erase_attempts(24) fpga_prog_attempts(25)
reset_attempts(26) fw_update_attempts(27) manifest_name(32) bin_name(40)
fw_update_status(48)`.

Файлы (по `driver_data`):

| вариант | манифест | образ |
|---|---|---|
| `LED_PANEL` | `yandex_led_panel.bin.manifest` | `yandex_led_panel.bin` |
| `LED_SCREEN` | **`yandex_led_screen_fpga.bin.manifest`** | **`yandex_led_screen_fpga.bin`** |

Манифест — текстовый, разбирается `sscanf` (`.rodata.str1.8+0xf60`, вызов
`0x29b0`; требуется ровно 5 полей):

```
fw_version = %hhu
crc16 = 0x%hx
user_code = 0x%x
uf_crc16 = 0x%hx
force_upd = %hhu
```

Логика (`lp_handle_firmware_update`, `0x2920`):

```
request_firmware(manifest); sscanf(...); if (!=5) -> "failed to parse firmware update manifest"
request_firmware(bin)
if (crc_itu_t(0, bin->data, bin->size) != manifest.crc16)
        -> "crc16 mismatch, expected: %hu, actual: %hu" (error)
if (!force_upd) {
    status = get_status_app_cmd()  (дважды)
    if (status.mode < manifest.fw_version)  -> "firmware update required"
    else                                    -> "firmware is up-to-date" (NOT_REQUIRED)
}
"start firmware update"
for (attempt = 1..4) {
    info("Update attempt: %d")
    if (select_jtag_mode() != 0) { error("can not run jtag mode"); continue; }
    if (lp_update_firmware() == 0) break;
    error("failed to update firmware: %d")
}
if (!ok) return UPD_STATUS__ERROR
if (select_spi_mode() != 0) return UPD_STATUS__ERROR ("failed to run SPI mode")
status = get_status_app_cmd()
if (status.mode != manifest.fw_version) return UPD_STATUS__UPDATE_FAIL
if (status.uf_crc != manifest.uf_crc16) return UPD_STATUS__UF_CRC_ERROR
info("firmware update completed"); info("trying to restart boot animation")
set_animation_app_cmd(1)
return UPD_STATUS__SUCCESS
```

`lp_update_firmware(jtag, fw_update)` (`0x2530`) — до 2 «раундов»:

```
for (round = 0; round < 2; round++) {
    for (i = 1; i <= 4; i++) { if (!jtag_erase_flash()) { erase_attempts = i; break; } sleep(200) }
    for (i = 1; i <= 4; i++) { if (!jtag_prog_fpga(fw, user_code)) { fpga_prog_attempts = i; break; } sleep(200) }
    fpga_hw_reset(jtag); reset_attempts = round+1;
    if (erase ok && prog ok) break;
}
```

`enum fw_update_status { NOT_REQUIRED=0, SUCCESS=1, UPDATE_FAIL=2, UF_CRC_ERROR=3,
ERROR=4 }` — видно в `fw_upd_status`
(`"not required" / "success" / "fail (try to work with previous FW)" /
"fail (CRC error of User Flash data)" / "fail"`).

Особенность: при `UPD_STATUS__ERROR` `probe` **возвращает 0, не регистрируя
framebuffer** (`0x2fc0 → 0x3298`). То есть «сломанная прошивка FPGA = нет
/dev/fb4», без внятной ошибки в userspace.

---

## 8. Что нужно для mainline-драйвера

### 8.1 fbdev, а не DRM

Контракт userspace — `/dev/fb4` + `write(1200 B)` + `close()`. Сохранить его
дешевле всего fbdev-драйвером (`drivers/video/fbdev/`), как уже сделано для
ring (`ledtrig-ringfb`, 24x1). DRM-tiny потребует fbdev-emulation, а она не
отслеживает прямые записи в mmap-буфер, т.е. текущий `screend` работать не
будет без правок (а править userspace не хочется).

Минимальный набор:

```c
static const struct fb_ops yndx_led_fb_ops = {
    .owner          = THIS_MODULE,
    .fb_check_var   = ...           /* только размеры из DT, менять xres/yres нельзя */
    .fb_set_par     = ...,          /* line_length = bpp/8 * xres_virtual */
    .fb_mmap        = fb_deferred_io_mmap,
    .fb_fillrect    = sys_fillrect, /* чтобы fbcon/логотип не падал */
    .fb_copyarea    = sys_copyarea,
    .fb_imageblit   = sys_imageblit,
    .fb_sync        = ...,
};
```

`fb_deferred_io` (delay ~20 мс) + `fb_sync` закрывают и `write()+close()`, и
mmap-сценарий; `fb_pan_display` — принудительный флаш. Вендорский драйвер
`fillrect/copyarea/imageblit` не имел вовсе (fbcon на нём не работал) — в
mainline их лучше включить, иначе `fbcon`/`fb_logo` могут уронить ядро.

### 8.2 Транспорт

* `spi_setup()`: mode 0, 8 бит, `max_speed_hz` из DT (4 МГц; для отладки можно
  1 МГц — вендор для bootloader-протокола использовал именно 1 МГц).
* CRC: `crc_itu_t()` уже есть в ядре (`lib/crc-itu-t.c`), экспортируется.
* Две транзакции на команду (`tx` + `rx 35`) — сохранить, это не «оптимизация»,
  а требование протокола.
* Ретраи: 5 попыток, `usleep_range(10000, 20000)`, бит `FRM_OVF` (0x08)
  игнорировать.
* Ответ проверять «остаточно»: `crc_itu_t(0, buf, 35) == 0` и `buf[1] == 1`
  (mode). Логировать `buf` при ошибке — сильно ускорит отладку.
* Для первого bring-up достаточно только `GET_STATUS` + `SHOW_PIC` +
  `SET_BRIGHTNESS`; `SET_ANIMATION`/power-saving/touch/UART-мост/gpiochip — потом.

### 8.3 Формат кадра

Оставить fb 24bpp BGR (как у вендора: red@16, green@8, blue@0, `line_length=75`),
конверсию `max(R,G,B)` — 1 байт/пиксель, 400 байт. Разрешение брать из DT
(`width`/`height` или `yandex,panel-size = <25 16>`) и **проверять** по ответу
`GET_STATUS`/по размеру, который вернёт FPGA (в вендорском коде разрешение берётся
из таблицы по `board_rev`, что для mainline неудобно).

### 8.4 GPIO

* `reset-gpios` (ACTIVE_LOW): сброс = низкий 10 мс, отпустить, подождать
  300 мс, затем до 10 попыток `GET_STATUS` — как в `devm_sa_device_init`, но
  без 2-секундных пауз.
* `jtag_sel`: на время работы — SPI-режим (высокий).
* JTAG (TCK/TDI/TMS/TDO, `jtag_reconfig`) — **не нужен** для обычной работы.
  Если понадобится прошивка FPGA — либо отдельный маленький bit-bang драйвер,
  либо `gpiod` прямо в основном драйвере; вместо RMW регистра
  `0xff634468` в mainline используем `gpiod_set_value(jtag_tck)` (тот же
  регистр, но через gpiolib; проверено, что `jtag_tck` уже запрашивается
  дескриптором). Задержки — `ndelay()`/`udelay(1)`.
* `at91`-подобного «JTAG-gpio» класса в mainline нет; проще держать это в
  собственном файле драйвера (как `jtag_gw.c` у вендора) и **не** включать в
  первый коммит.

### 8.5 DT (набросок, не для коммита)

```dts
&spicc1 {
    status = "okay";
    pinctrl-0 = <&spicc1_pins>;
    cs-gpios = <&gpio GPIOH_6 GPIO_ACTIVE_LOW>;

    panel: led-screen@0 {
        compatible = "yandex,led-screen";      /* новый, не "ya,led_screen" */
        reg = <0>;
        spi-max-frequency = <4000000>;
        reset-gpios = <&gpio GPIOX_8 GPIO_ACTIVE_LOW>;
        max-brightness = <200>;
        width = <25>;
        height = <16>;
        /* jtag-* — только если будем прошивать FPGA */
    };
};
```

### 8.6 Риски

1. **Первый байт ответа** (35-байтная рамка) не используется драйвером —
   если на mainline читать так же (35 B), поведение совпадёт; менять длину
   нельзя, иначе поедет CRC.
2. **Ориентация/порядок** пикселей не проверялись на железе. `screend`
   рисует top-down слева-направо, но не факт, что панель не транспонирована.
3. **Первый `probe` на холодном старте**: FPGA могла ещё не сконфигурироваться;
   вендор ждёт до 10 попыток и делает hard reset. Без этого можно получить
   «нет /dev/fb4».
4. **Поля `mode` / `last_cmd_status`**: если прошивка FPGA от другой ревизии
   вернёт `mode != 1`, вендорский код считает устройство мёртвым. Надо мягко
   логировать и продолжать.
5. **`FRM_OVF`**: переполнение очереди кадров — штатное событие при быстрой
   перерисовке; не считать ошибкой (иначе будут ложные `-EIO`).
6. **Разрешение из таблицы по `board_rev`** — единственный источник истины в
   вендорском коде. На железе надо один раз посмотреть `GET_STATUS`
   (`board_rev`, `xres`, `yres` в `quasar_led_screen`-варианте) и зафиксировать.
7. **Обновление прошивки FPGA** — самый опасный путь (JTAG bit-bang + erase
   flash). Если сделать неправильно, можно «окирпичить» панель до перепайки.
   Отдельный этап, с дампом текущего flash (`uf_crc`, `USER CODE`) до записи.
8. **`/dev/fb4` vs `/dev/fb0`**: вендор даёт fb4 (после других фреймбуферов).
   `screend` жёстко открывает `/dev/fb4` — либо сохранить номер через
   `fb_info.node`, либо симлинк в userspace.

### 8.7 Сверка с наброском драйвера (`drivers/video/fbdev/atrios-ledpanel.c`)

В ветке уже лежит набросок mainline-драйвера, написанный по
`quasar_led_screen` (`sa_*`, API v9). По данным этого отчёта он **не совпадёт
с прошивкой, которая работает на плате**: на этой плате биндится
`gowin_led_device`, и его протокол (`pa_*`) отличается в шести местах, каждого
из которых достаточно, чтобы панель не заработала:

| | `quasar_led_screen` (sa, v9) — как в наброске | `gowin_led_device` (pa) — что реально на шине |
|---|---|---|
| опрос статуса | 20 B, tx = 20 нулей + CRC, `speed_hz = 1 МГц` | 35 B, tx = NULL, 4 МГц |
| начало ответа | `resp[0] = api_version` | байт 0 не используется, `mode` в байте 1 |
| `last_cmd_status` | `resp[8]`, значение (`21` = CRC error) | байт 16, битовая маска (`1/2/4/8/0x10/0x20/0x40`) |
| CRC ответа | `le16(resp[14..15]) == crc_itu_t(0, resp, 14)` | `crc_itu_t(0, buf, 35) == 0` |
| `SHOW_PIC` | `07 <img> <delay u16 LE>` | `07 00 <delay u16 LE> <img>` |
| разрешение | из ответа (`xres`, `yres`) | из таблицы `rev2res_screen[board_rev]` |
| реакция на ошибку | одна попытка, `-121` | повтор по битам 1/2, `FRM_OVF` игнорируется |

Проверки, которые это подтверждают:

* `compatible` узла = `"ya,led_screen", "atri,led-panel"` → `of_modalias_node`
  → `"led_screen"` → `led_device_id` → `driver_data = 1` → `LED_SCREEN` (§1);
  у `quasar_led_screen` `compatible = "quasar_led_screen"`, которого в этом DT
  нет.
* `/etc/init.d/S01modules` грузит **оба** модуля (`insmod gowin_led_device.ko`,
  затем `quasar_led_screen.ko`); второй просто не находит устройство.
* `screend` пишет 1200 B и закрывает fd — это ровно контракт
  `gowin: fb_release → lp_fb_sync`.

Практический вывод: набросок стоит либо переписать на `pa_*`, либо выбирать
протокол в `probe` по факту ответа (дешёвая автодетекция: прочитать 35 B и
проверить `crc_itu_t(0, buf, 35) == 0 && buf[1] == 1`; если не сходится —
читать 20 B и проверять `sa_parse_response`). Второй вариант полезен, если на
плате встречается и прошивка из `quasar_led_screen`.

Замечание по DT: набросок биндится по `atri,led-panel` (это второй
`compatible` вендорского узла), т.е. на текущем DTB он **будет** загружен — и
именно поэтому важно, чтобы протокол внутри соответствовал `pa_*`. Иначе
получится «/dev/fb4 есть, а панель молчит»: 20 байт `pa`-ответа,
разобранные как `sa`-ответ, дадут ошибку CRC (или мусор в полях), а ошибки
`SHOW_PIC` в наброске только логируются.

---

## 9. Что осталось неизвестным / проверить на железе

1. **Логи SPI.** Снять логическим анализатором `CS/CLK/MOSI/MISO` на один кадр
   от `screend`: подтвердить 406 B + 35 B, mode 0, 4 МГц, полярность CS и
   значение первого байта MISO.
2. **`board_rev`, `mode`, `uf_crc`** — прочитать `GET_STATUS` на живой панели
   (`print_hex_dump` в вендорском драйвере уже есть; можно включить
   `dynamic_debug`/`echo 8 > /proc/sys/kernel/printk`).
   Ожидание: `board_rev = 1`, `mode = 1`, `fw_version` = версия FPGA-приложения.
3. **Что за байт 0 в ответе** (dummy / sync / длина) и не «съезжает» ли рамка
   на другой прошивке.
4. **Семантика `delay`** в `SHOW_PIC`: пауза показа текущего кадра, мс.
   Проверить: `echo 100 > /sys/.../frame_queue/next_frame_delay` и посмотреть,
   меняется ли плавность анимации и растёт ли `frames_in_queue`.
5. **Адрес `0xff634468 бит 7` = TCK (GPIOH_7)?** Не совпадает с раскладкой
   mainline `pinctrl-meson-g12a` (там `GPIOH` out = +0x38 от окна
   `0xff634440`). Нужно либо подтвердить по регистровой карте SM1, либо просто
   использовать `gpiod_set_value()` (что для mainline и так предпочтительно).
6. **`jtag_sel`**: DTB говорит AO-bank пин 14, в задании — GPIOE_2. Проверить
   мультиплексор на плате (какой уровень = SPI).
7. **Формат `yandex_led_screen_fpga.bin`**: проверить, что это Gowin `.fs`
   (строки по 256 B с заголовком `GW1N`), и что `user_code`/`uf_crc16` из
   манифеста совпадают с реальным чипом (`read_code(0x13)` / `read_code(0x41)`).
8. **Чем конфигурируется FPGA на холодном старте** (bootloader? сама из
   flash?) — от этого зависит, нужен ли JTAG в mainline вообще.
9. **Точная геометрия панели** (25x16 подтверждено таблицей `rev2res_screen[1]`
   и `screend`, но физическая ориентация/шаг — нет).
10. **`enum tap_states`** — имена состояний восстановлены по значениям
    (1/4/5/11/12/16); в DWARF enum попал без членов (`[предположение]`).

---

## 10. Как воспроизвести

```sh
KM=board/yandex/station2/rootfs_overlay/lib/modules/4.9.113
nm -n -C $KM/gowin_led_device.ko            # смещения, имена, T/t
aarch64-linux-gnu-objdump -dr --no-show-raw-insn $KM/gowin_led_device.ko
readelf --debug-dump=info   $KM/gowin_led_device.ko   # типы, локалы, decl_file:line
readelf --debug-dump=decodedline $KM/gowin_led_device.ko
readelf -r $KM/gowin_led_device.ko          # какие .text-функции в fb_ops/bl_ops
```

Полезные факты: `.text` модуля грузится по 0, поэтому смещения из `nm` = адреса
в листинге `objdump`; `.rodata` — файловое смещение `0x54a0` (gowin) /
`0x3078` (quasar), `.rodata.str1.8` — `0x5970` / `0x34f8`.

## 11. Evidence

* `docs/evidence/screen-gowin-artifact.json` — REA `inspect-artifact`
  (`sha256 c020403c…364b`).
* `docs/evidence/screen-quasar-led-screen-artifact.json` — REA
  (`sha256 10f7ea26…6245`).
* Вендорский DTS: `AtriOS-linux/tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts:2242`.
* Userspace: `board/yandex/station2/rootfs_overlay/usr/lib/atrios/screend`.
