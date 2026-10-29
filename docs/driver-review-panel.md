# Review — `drivers/video/fbdev/atrios-ledpanel.c`

Ревью независимое, код не менялся. Ветка `mainline`, коммит-база `da30a0b`.
Сверка: `docs/screen.md` (PA/SA RE), `docs/ledpanel-driver.md`,
`docs/dts-review.md`, `dts/meson-sm1-yandex-station-max.dts:620-632` и
дизассемблер вендорских модулей
`board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/{gowin_led_device,quasar_led_screen}.ko`
(sha256 `c020403c…364b` / `10f7ea26…6245`, совпадают с `docs/screen.md`).

Сборка и стиль:

```
make -C /home/leftymods/mainline-src/linux-7.3 O=out/linux/build ARCH=arm64 \
     CROSS_COMPILE=aarch64-linux-gnu- W=1 drivers/video/fbdev/atrios-ledpanel.o
  → CC drivers/video/fbdev/atrios-ledpanel.o, 0 предупреждений (в т.ч. с W=1)

scripts/checkpatch.pl --strict --no-tree -f drivers/video/fbdev/atrios-ledpanel.c
  → total: 0 errors, 0 warnings, 0 checks, 976 lines checked
```

Файл в дереве ядра (`linux-7.3/drivers/video/fbdev/atrios-ledpanel.c`)
побайтно совпадает с файлом в репозитории.

## Вердикт по протоколу PA

**PA реализован верно и совпадает с `gowin_led_device` по всем проверяемым
местам**: кадр `07 00 <delay u16 LE> <400 B> <crc16 BE>` = 406 B, статус —
чистое чтение 35 B, CRC проверяется остаточно (`crc_itu_t(0,buf,35)==0`),
`mode = buf[1]`, `fw_version = buf[2]`, `last_cmd_status = buf[16]`, бит 3
(`FRM_OVF`) не считается ошибкой, `SET_BRIGHTNESS = 1b <val> + crc` = 4 B,
CRC-16/XMODEM считается по длине без CRC и уходит big-endian. Блокеров
протокола нет; на этой плате драйвер должен работать (при условии, что
`jtag-sel` в SPI-режиме и FPGA сконфигурирована — см. п. 5/«неизвестно»).

Остальные замечания — жизненный цикл fbdev, teardown, отсутствие ретраев и
несколько рисков probe. Ни одно из них не ломает PA-обмен на работающей
плате.

## Находки (по убыванию серьёзности)

| # | строка | проблема | серьёзность | исправление |
|---|---|---|---|---|
| 1 | `atrios-ledpanel.c:791` + `:723` + `:579-593` | **Use-after-free приватных данных.** `lp` выделен `devm_kzalloc()` на `&spi->dev`, а `info->par = lp` живёт в `fb_info`, который переживает `remove()`: `fb_info` держит ссылку, пока открыт `/dev/fbN` (`fb_open()` → `get_fb_info()`). После `remove()` devres освобождает `lp`, а последующий `write()`/`close()` на этом fd вызывает `ledpanel_write()`→`ledpanel_schedule_push()` (`mutex_lock(&lp->lock)`, строка 217) и `ledpanel_destroy()` (`cancel_delayed_work_sync(&lp->push)`, строка 590) уже на освобождённой памяти. Комментарий в `remove()` (`:925-929`) прямо заявляет, что этот сценарий поддержан, — фактически он падает. | высокая (только unbind/device-remove с открытым fd; для `=y` без unbind не проявляется) | держать `lp` там же, где `fb_info`: `info = framebuffer_alloc(sizeof(*lp), dev); lp = info->par;` (как `vfb.c:82-86`, `efifb.c:253-268`), либо запретить освобождение `lp` до `fb_destroy` (kref/`info->par` = NULL + проверки во всех fb-колбэках) |
| 2 | `:586-593`, `:943-946` | **Нарушен контракт `fb_destroy`/`framebuffer_release` этого ядра.** В v7.3 `put_fb_info()` вызывает `fb_destroy` на последней ссылке, а `fb_destroy` обязан сам сделать `fb_dealloc_cmap()` + `framebuffer_release()`; `remove()` должен вызывать только `unregister_framebuffer()` (эталон: `vfb_destroy`/`vfb_remove`, `efifb.c:253-268`, `simplefb.c:102-124`). Здесь наоборот: `remove()` вызывает `fb_dealloc_cmap()` и `framebuffer_release()` сразу после `unregister_framebuffer()`. Если fb ещё кем-то открыт, `refcount_read(&info->count) != 0` → `WARN_ON()` в `framebuffer_release()` (`drivers/video/fbdev/core/fb_info.c:66-79`) и `fb_info`/cmap утекают навсегда (в `fb_destroy` их никто не освобождает). | высокая (WARN + утечка; срабатывает вместе с п. 1) | свести teardown к канону: `remove()` → `unregister_framebuffer(info)`; `fb_destroy()` → `cancel_delayed_work_sync()` + `vfree(screen_buffer)` + `fb_dealloc_cmap(&info->cmap)` + `framebuffer_release(info)`. Ошибочные пути probe тоже выровнять |
| 3 | `:807` (`ledpanel_reset()`, `:650-671`) | **Безусловный сброс FPGA на каждом probe.** Вендорский `gowin_led_device` в обычном probe сбрасывает только `msleep(300)`, а импульс на `reset` даёт лишь в пути обновления прошивки (`fpga_hw_reset`). Если битстрим не лежит во flash FPGA и загружается кем-то извне (в `docs/screen.md:1098` это открытый вопрос), импульс оставит панель тёмной до перепрошивки, и драйвер этого не отличит от «нет панели». | высокая (риск, проверяется только на железе) | не сбрасывать по умолчанию: сначала detect, reset — только если detect не удался и это разрешено свойством DT/параметром модуля (`reset-on-probe`); либо вернуть вендорские 300 мс и не трогать линию вообще |
| 4 | `:371-377`, `:473-479`, `:628-632` | **Нет ретраев, `CMD_OVF` (бит 2) трактуется как фатальная ошибка.** Вендор: `SHOW_PIC` — 1 попытка, `SET_BRIGHTNESS` — 5; `CMD_OVF` → `usleep_range(10,20)` и **повтор**, `CRC_ERROR` (бит 1) → повтор; только после цикла — разбор битов (`lp_write` 0x4b70…0x4bbc, `show_pic_app_cmd` 0x4f58 `w3=1`, `set_brightness_app_cmd` 0x4fc4 `w3=5`). В драйвере одна ошибка → `-EIO`, кадр не отправлен, `shadow` не обновлён; повтор будет только на следующую запись (у `screend` — через кадр). | средняя | добавить цикл попыток (1 для кадра, 5 для яркости) с `usleep_range(10000, 20000)`; бит 2 — повтор, не ошибка; остальные биты — ошибка |
| 5 | probe (нет кода), DTS `:631` | **`jtag-sel` не переводится в SPI-режим.** Вендор в probe делает `devm_gpiod_get(dev,"jtag_sel",GPIOD_OUT_HIGH)` (0x2e00, `docs/screen.md:190`), т.е. явно выбирает SPI-ветку мультиплексора. Драйвер `jtag-*` осознанно не запрашивает (это правильно для TCK/TDI/TDO/TMS на общих пинах `spicc1`), но `sel` висит на AO-банке (`GPIOE_2`) и с `spicc1` не конфликтует — его можно и нужно взять. Если загрузчик оставил JTAG, probe упадёт с `no known panel firmware found`, хотя железо исправно. | средняя | опционально `devm_gpiod_get_optional(dev, "jtag-sel", GPIOD_OUT_HIGH)` до `spi_setup()`/detect; в DTS свойство уже есть |
| 6 | `:801-802` | **Скорость SPI молча занижается до 1 МГц** при `spi-max-frequency = <4000000>` в DT; вендорский gowin работает на 4 МГц. Кадр 406 B на 1 МГц ≈ 3.3 мс + 35 B статус ≈ 0.3 мс на каждый кадр (при `refresh_ms=20` запас есть, но это 18 % бюджета). Плюс параметр `spi_hz` перезаписывает `spi->max_speed_hz` спай-устройства — «грязновато», но допустимо (вендор для bootloader тоже 1 МГц). | низкая (задокументировано в `docs/ledpanel-driver.md:247-249`) | оставить, но по умолчанию брать значение из DT (`spi_hz=0`), а 1 МГц — только как отладочный override |
| 7 | `:826-835`, `:875-878`; DTS `:620-632`; patch 0005 | **Нет DT-binding и не проверяется `board_rev`.** `width`/`height` — не namespaced свойства, в DTS их нет (работают только дефолты 25x16); `Documentation/devicetree/bindings/` в патче отсутствует, так что `dtbs_check` поругается на `yandex,led-screen`, `max-brightness`, `jtag-*`. Вендорский probe отвергает `board_rev != 1` (`unsupported board revision`, 0x1470), а драйвер `buf[3..4]` вообще не читает — панель другой ревизии будет молча гнаться как 25x16. | средняя (для upstream), низкая (для этой платы) | добавить YAML-binding (`yandex,panel-width/height` или `yandex,panel-size`), вендор-префиксы; логировать `board_rev` и предупреждать при `!= 1` |
| 8 | `:877-878` | `max-brightness = <0>` (или отсутствие) даёт `props.max_brightness = 0`; `backlight_device_register()` такое не проверяет, а `backlight_update_status()` отправит `1b 00`. Мелочь, но DT-ошибка пройдёт молча. | низкая | `if (!value \|\| value > 255) value = DEFAULT;` + `dev_warn` |
| 9 | `:886` | Имя backlight берётся из имени DT-узла (`panel@0` → `panel`) — общее имя, возможна коллизия в `/sys/class/backlight/`; в `docs/ledpanel-driver.md:216` оно подаётся как стабильный путь. | низкая | `devm_backlight_device_register(..., "atrios-ledpanel", ...)` или `dev_name(dev)` |
| 10 | `docs/ledpanel-driver.md:285-287`, `docs/dts-review.md:85` | Устаревшие утверждения: «`CONFIG_FB_ATRIOS_LEDPANEL=y` ещё нужно добавить» (уже есть, `configs/atrios.config:77`) и «no driver yet» для `panel@0`. | низкая (доки) | поправить обе строки |
| 11 | `:101-107` | `LEDPANEL_PA_SPKT_ERROR/LPKT_ERROR/UART_OVF` объявлены и не используются (биты 4/5/6 просто попадают в «UNKNOWN»). | nit | либо использовать в `ledpanel_str_status()`, либо убрать |

Замечаний по стилю/checkpatch нет — специально не раздуваю.

## Соответствует вендору (проверено по дизассемблеру)

| Что | Драйвер | Вендор / доказательство |
|---|---|---|
| CRC-16/XMODEM, init 0, poly 0x1021, без отражения и финального xor | `ledpanel_crc16()`, `:200-213` | `crc_itu_t` в обоих модулях; численно проверено: `crc_itu_t("123456789")=0x31c3`, остаток по 35 B = 0 |
| CRC команды: по длине **без** CRC, `rev16`+`strh` → BE | `:235-239` | `lp_write` 0x4ad8…0x4ae0 (`crc_itu_t(0,buf,size-2)`, `rev16`, `strh`); `sa_write_v9` 0x2410…0x2430 |
| Длина tx включает CRC | `len + 2`, `spi_write(lp->tx, len)` | `lp_write` 0x4b3c `str w21` = полный размер, 0x4b50 `spi_sync` |
| PA `SHOW_PIC` = `07 00 <delay u16 LE> <400 B>` + CRC = 406 B | `:459-465` | `show_pic_app_cmd` 0x4f30 (`07`), 0x4f40 (`buf[1]=0`), 0x4f48 (`strh` в +2 = LE), 0x4f4c (`memcpy` в +4), 0x4f50 (`len = size+6`) |
| PA статус: 35 B, `tx_buf = NULL`, CRC остаточно | `:260-281` | `get_lp_status` 0x4608 (`#35`), 0x4654 (tx=0, rx=buf), 0x46dc (`crc_itu_t(0,buf,35)`), 0x46e8 (`tst`/`b.eq`) |
| PA поля: `mode@1` (== 1), `fw@2`, `board_rev@3..4`, `frames_in_queue@15`, `last_cmd_status@16` | `:274-278`, `:688` | `get_lp_status` 0x466c…0x46b0: buf[1]→resp[0], buf[2]→resp[1], buf[3..4]→resp[2..3], buf[15]→resp[12], buf[16]→resp[13] |
| PA `last_cmd_status` — битовая маска; `FRM_OVF` (бит 3) не ошибка | `:371`, `:104` | `lp_write` 0x4bdc…0x4bf0 (`and w0,w0,#0xfffffff7` перед проверкой) |
| `SET_BRIGHTNESS` = `1b <val>` + CRC = 4 B | `:626-628` | `set_brightness_app_cmd` 0x4fb4 (`0x1b`), 0x4fc8 (`x2=4`), 0x4fc4 (`w3=5`) |
| Яркость кэшируется, команда под мьютексом | `:621-633` | `lp_bl_update_status` 0x1b00: `strb` в кэш, `mutex_lock`, `set_brightness_app_cmd` |
| Кадр: 1 байт/светодиод = `max(R,G,B)`, red@16/green@8/blue@0, 400 B, построчно | `:425-443` | `lp_fb_sync` 0x1c14…0x1c64 (`and 0xff` для B, `lsr 16` R, `lsr 8` G, два `csel`) |
| 24 bpp, `line_length = 75`, `smem_len = PAGE_ALIGN(1200)` | `:726`, `:750`, `:753` | `docs/screen.md:235-239`; вендор тоже page-align’ит |
| SA-статус: 22 B `00*20 + CRC BE` в полный дуплекс, ответ 20 B, CRC по 14 B сравнивается **LE** из `[14..15]` | `:283-306` | `sa_none_unsafe` 0x217c…0x21e0 (len 22), 0x2224 `spi_sync`, `sa_parse_response` 0x2360 `ldrh [x19,#14]`, 0x2364 `crc_itu_t(0,buf,14)` |
| SA-поля: `api@0`, `xres@1`, `yres@2`, `depth@3`, `temp@6`, `fw@7`, `status@8` | `:308-316` | `sa_parse_response` 0x2294…0x2338 |
| SA `SHOW_PIC` = `07 <img> <delay u16 LE>` + CRC = 405 B | `:466-471` | `sa_show_pic` 0x2e60…0x2e90 (`strh` в `cmd_buf+1+len`, `x2 = len+3`) |
| CRC пишется только для PA (всегда) и SA api ≥ 9 | `:817-819` | таблица `.rodata+0x308` (`docs/screen.md:562-573`), `sa_write_v9` vs `spi_write` |
| Поддержаны api 5/6/7/9, 8 — нет | `:395-398` | та же таблица |
| `max-brightness` из DT, clamp 255, стартовое значение = max | `:875-878` | `docs/screen.md:326-329` |

Проверка CRC численно (python-модель того же алгоритма): остаток по 35 B с
дописанным BE-CRC равен 0; LE-сравнение ответа SA сходится. Порядок байт в
драйвере и вендоре идентичен.

## Осознанные отличия (не дефекты, но их надо помнить)

1. **Обновление кадра через delayed work + дедупликация по `shadow`**
   (`:449-499`) вместо «только `release()`/`FBIOPAN_DISPLAY`» у вендора.
   Правильное решение: покрывает и `write()+close()` (`screend`), и долгие
   fd, и mmap+pan. Побочный эффект: если панель потеряла кадр (сброс FPGA),
   драйвер не перешлёт его, пока содержимое не изменится.
2. **Геометрия PA из DT/дефолта**, а не из `rev2res_screen[board_rev]`
   (вендор). Для этой платы эквивалентно (25x16), но `board_rev` не
   проверяется (п. 7).
3. **Сброс 10 мс / ожидание 2 с** вместо 300 мс у gowin (взято из quasar,
   `docs/ledpanel-driver.md:182-184`) — см. п. 3.
4. **1 МГц** вместо 4 МГц (п. 6).
5. **`mode != 1` проверяется только при detect**, а не после каждой команды
   (вендор возвращает `-EPROTO` в `get_lp_status`). Мягче — это правильно для
   bring-up.
6. **`fix.id = "atrios-ledpanel"`**, нет `open_fb_count`, hwmon, sysfs
   `frame_queue/next_frame_delay` (`next_frame_delay` всегда 0), нет JTAG,
   `bl_*`-обновления прошивки, touch/UART/gpio-экспандера — как заявлено в
   `docs/ledpanel-driver.md:261-270`.
7. **`frame_format=1`** (сырой RGB888) — гипотеза, в бинарях подтверждения нет
   (`docs/ledpanel-driver.md:250-251`). Код безопасен: длина tx пересчитана
   (`:862-865`).
8. **`jtag-*` в DTS игнорируются** — осознанно и полезно: TCK/TDI/TDO/TMS
   делят пины со `spicc1` (`docs/dts-review.md:92-99`), запрос этих GPIO дал
   бы `-EBUSY`. Не хватает только `sel` (п. 5).
9. **`spi-max-frequency` из DT фактически не используется** (перекрыт
   `spi_hz`) — осознанно, но DT теперь врёт о реальной скорости.

## Неизвестно без железа

1. Безопасен ли импульс `reset` (п. 3) — зависит от того, чем
   конфигурируется FPGA на холодном старте (`docs/screen.md:1098`).
2. Состояние `jtag-sel` после загрузчика (п. 5).
3. Ориентация/порядок пикселей (top-down, слева-направо) — предположение
   `screend`, `docs/ledpanel-driver.md:338-339`.
4. Что в байте 0 35-байтного ответа (драйвер его не использует — как вендор;
   менять длину нельзя, иначе поедет CRC, `docs/screen.md:1008-1010`).
5. Возвращает ли прошивка api 5/6/7 корректный CRC в ответе: драйвер (как и
   `sa_parse_response`) требует его всегда; при таком firmware автодетект SA
   может не сработать.
6. Стабильность на 4 МГц (сейчас проверяемо только через `spi_hz=4000000`).

## Мелочи, которые стоит знать при отладке

* `fb_mmap` передаёт `vma->vm_pgoff` (`:576`), вендор — 0; для `mmap(...,0)`
  эквивалентно, `remap_vmalloc_range()` сам проверит границы.
* `fb_sys_write()` ограничен `screen_size` = 4096, поэтому «лишние» байты в
  `/dev/fbN` не ломают кадр (конвертируется только 25x16).
* После `LEDPANEL_MAX_DIM`-проверки (`:837-844`) 0 или >255 заменяется на
  25x16 с предупреждением — для SA `xres/yres = 0` не приводит к делению на 0.
* PA-детект делает 3 попытки (≈30 мс), SA — 10 (≈100 мс); при холодном
  старте панели этого может не хватить, а повторов с новым сбросом нет
  (`docs/ledpanel-driver.md:244-246`).
