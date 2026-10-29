# Review — input/LED: `ledtrig-ringfb`, `atrios-keypad`, `rotary-volume`

Независимое ревью, код не менялся. Ветка `mainline`, коммит `8b06882`
(«fb: fix the lifetime of the ring/panel framebuffers and panel speed»,
2026-10-29 10:19). Рабочее дерево чистое, поэтому «diff» — это состояние
файлов на этом коммите.

Сверка: вендорские модули
`board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/{quasar_led_ring,leds-is31fl32xx,rotary-volume}.ko`
(DWARF + `aarch64-linux-gnu-objdump`, `pahole`), вендорский DTS
`tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts`, `docs/{ring,rotary,buttons,screen}.md`,
`dts/meson-sm1-yandex-station-max.dts`, upstream 7.3
(`drivers/leds/led-triggers.c`, `drivers/video/fbdev/core/*`,
`drivers/input/input.c`, `drivers/irqchip/irq-meson-gpio.c`).

## Сборка и стиль

```
make -C /home/leftymods/mainline-src/linux-7.3 O=out/linux/build ARCH=arm64 \
     CROSS_COMPILE=aarch64-linux-gnu- W=1 \
     drivers/leds/trigger/ledtrig-ringfb.o \
     drivers/input/misc/atrios-keypad.o drivers/input/misc/rotary-volume.o
  → keypad.o, rotary-volume.o: CC без предупреждений
  → ledtrig-ringfb.o: ОШИБКА (см. находку 1)

checkpatch.pl --no-tree -f <три .c>  → 0 errors, 0 warnings
checkpatch.pl --no-tree <0003/0004/0006.patch> → ERROR: trailing whitespace
     (пустые строки Kconfig с пробелом; косметика, не блокер)
```

## Находки (по убыванию серьёзности)

| # | файл:строка | проблема | серьёзность | как исправить |
|---|---|---|---|---|
| 1 | `drivers/leds/trigger/ledtrig-ringfb.c:230` и `:309` | **Не собирается.** `ringfb_destroy` используется в `fb_ops` (строка 230) до объявления (строка 309). gcc: `error: 'ringfb_destroy' undeclared here` + `warning: defined but not used`. Ошибка воспроизводится и в репозитории, и в дереве ядра (`mainline-src/linux-7.3`), т.е. патч 0003 в текущем виде не компилируется. | **blocker** | Добавить `static void ringfb_destroy(struct fb_info *info);` перед `ringfb_ops` (или перенести определение выше `ringfb_ops`). |
| 2 | `drivers/leds/trigger/ledtrig-ringfb.c:298-305` | **Double free / UAF в error-path probe.** После успешного `register_framebuffer()` при ошибке `led_trigger_register()` вызывается `unregister_framebuffer(info)`; в 7.3 это `do_unregister_framebuffer()` → `put_fb_info()` → `fb_destroy` = `ringfb_destroy()`, которая уже делает `vfree(screen_buffer)`, `fb_dealloc_cmap()` и `framebuffer_release(info)`. Затем код проваливается в `err_free_cmap/err_free_screen/err_free_fb` и повторно освобождает `cmap`, `screen_buffer` и `info`. Сработает, например, при уже зарегистрированном имени `led_ring_fb` (второй узел `led-ring-fb`); при открытом `/dev/fbN` бесплатно портит и его. | **major** (только error-path; память ядра) | В `err_unregister_fb` после `unregister_framebuffer(info)` сразу `return ret;` — очистку делает `fb_destroy` (в т.ч. позже, если fb ещё открыт). Ручной `fb_dealloc_cmap/vfree/framebuffer_release` оставить только путям до `register_framebuffer()`. |
| 3 | `drivers/input/misc/rotary-volume.c:91-98` | **Направление вращения инвертировано относительно вендора.** Вендор пакует `state = (val0 << 1) \| val1` (bit0 = **второй** GPIO) и на `state==1` шлёт `keycodes[0]` (PAGEUP), на `state==2` — `keycodes[1]` (PAGEDOWN). Драйвер пакует `val << i` (bit0 = первый GPIO) и на падении первого GPIO (второй high) шлёт `keycodes[1]` (PAGEDOWN), на падении второго — `keycodes[0]` (PAGEUP). Один и тот же физический щелчок даёт противоположный код; `rotaryd` (`KEY_PAGEUP`=громче) будет крутить громкость назад. Доказательство: `rotary_volume_work+0x30..0x94` — `lsl w19,w19,#1` перед `orr`, `cmp #1 → [x21+32]` (`keycodes[0]`), `cmp #2 → [x21+36]` (`keycodes[1]`); `pahole -C rotary_volume` подтверждает `keycodes` по смещениям 48/52. | **major** (функция работает, но в другую сторону) | Поменять местами `keycodes[0]`/`keycodes[1]` в двух ветках (или пакуется как вендор: `state \|= val << (1 - i)`), поправить таблицу `docs/rotary.md:42-46`. |
| 4 | `drivers/leds/trigger/ledtrig-ringfb.c:35,129` + `patches/linux/0002-leds-is31fl32xx-gva.patch:38-45,54-55` | **`max_brightness` 200 — не вендорское поведение.** Вендор проверяет `led->max_brightness == 255` (`led_ring_trigger_activate+0x1c`: `cmp w0,#0xff`, в сообщение передаётся 255), а `max-brightness-scale=200` применяет **внутри** `is31fl32xx_brightness_set` (`value*scale/255`, `is31fl32xx_brightness_set+0x20`). Патч 0002 превращает scale в `led_classdev.max_brightness=200` и не масштабирует `brightness_set`, поэтому байты кадра клампятся, а не масштабируются: 128 → 128 (у вендора ~100), 180 → 180 (~141), 220 → 200 (~172); пик 255→200 совпадает. Дополнительно: любое значение `max-brightness-scale` ≠200 (включая отсутствие свойства, тогда будет 255) даёт `max_brightness≠200`, и все 72 LED получат `-EINVAL` — кольцо погаснет. `docs/ring.md:23-24,55`, `docs/dts-review.md:53,80` описывают это как вендорское — неверно. | **major** (кривая яркости/связность; на текущем DT кольцо работает) | Либо повторить вендор: `max_brightness=255`, scale — в `brightness_set`, проверка 255 в триггере; либо оставить 200 как осознанную адаптацию, но отвязать проверку триггера от scale и исправить доки. |
| 5 | `drivers/leds/trigger/ledtrig-ringfb.c:168-193,113-114` | **Устаревший `shadow` после deactivate.** Deactivate чистит каналы, а LED-core после него выставляет каждому LED `LED_OFF`; `ring->shadow` не сбрасывается. При повторной активации (echo none/led_ring_fb, unbind/rebind I2C-контроллера) work видит `memcmp(shadow, screen_buffer)==0` и не пушит ничего — кольцо остаётся тёмным, пока userspace не запишет **другой** кадр. | **major** (тихий отказ после recovery/re-arm) | В deactivate (или при старте в activate) обнулять `shadow`/ставить флаг «пушить принудительно», чтобы первый refresh всегда отправлял текущий кадр. |
| 6 | `drivers/leds/trigger/ledtrig-ringfb.c:158` | **«Всё или ничего»: work стартует только при `nchannels==72`.** Один незарегистрированный/переименованный LED или упавший второй IS31FL3236A — и не работает всё кольцо; в логе только один `dev_warn` от activate. Вендор в `fb_sync` обходит список уже активированных узлов и драйвит доступные каналы (частичное кольцо). | minor | Запускать work при первом активированном канале; отсутствующие элементы массива уже пропускаются. Либо явно логировать «ring incomplete: N/72» после probe. |
| 7 | `drivers/input/misc/atrios-keypad.c:100-101,123,132-133` | **Полярность KEY_MICMUTE расходится с вендорским userspace.** Драйвер отдаёт logical-уровень (`GPIOD_IN` + `GPIO_ACTIVE_LOW` → нажатие = 1 для обеих клавиш), а вендорский `/usr/lib/atrios/rotaryd` имеет `MIC_MUTE_ACTIVE_LOW=True` и реагирует на `KEY_MICMUTE` со значением **0**: на mainline mute будет переключаться на отпускание, а не на нажатие (function-кнопка при этом работает, т.к. ждёт 1). U-Boot `gpio-keys` в этом же дереве тоже active-low (нажатие=1), т.е. драйвер последователен, а константа userspace — нет. | minor | Сверить на железе; вероятнее всего поправить `MIC_MUTE_ACTIVE_LOW=False` в `rotaryd` (или сделать полярность per-key в DT, если вендор реально читал raw). |
| 8 | `drivers/input/misc/rotary-volume.c:147-161,180-181` | **Нет валидации `key-codes` из DT.** Код передаётся в `input_set_capability()`/`input_report_key()` без проверки `< KEY_CNT`; при кривом DT (`key-codes = <0x1000 ...>`) `__set_bit()` выйдет за `keybit`. В keypad такая проверка есть (`atrios-keypad.c:223-228`). | minor (кривой DT) | Добавить ту же проверку `!code \|\| code >= KEY_CNT` в probe. |
| 9 | `drivers/leds/trigger/ledtrig-ringfb.c` (нет кода) | **Нет backlight-устройства.** Вендор регистрирует `backlight_device` (`bl_ops`, `props.brightness=max_brightness=255`) и в `fb_sync` умножает байт кадра на `bl->props.brightness/max_brightness`. Вендорский `light`/`rotaryd` пишут только fb, поэтому на практике это no-op, но `/sys/class/backlight/*` (сенсор освещённости, сторонние демоны) кольцо не затемнит. `docs/ring.md:66` утверждает, что backlight не нужен. | minor | Либо `devm_backlight_device_register()` + масштабирование в push, либо явно зафиксировать отказ в доке. |
| 10 | `docs/ring.md:23-24,55,66`; `docs/dts-review.md:53,80`; `docs/rotary.md:42-46` | **Доки противоречат и вендору, и коду:** «вендор требовал max_brightness 200» (на самом деле 255), «fbdev с deferred IO» (драйвер опрашивает буфер раз в 20 мс, `fb_deferred_io` не используется), таблица состояний rotary перепутана (bit0 у вендора — второй GPIO), «backlight не нужен» без оговорки. | minor (docs) | Поправить строки; в rotary.md поменять местами GPIO в таблице или переписать её в терминах «первый/второй GPIO упал». |
| 11 | нет тестов | Нет ни KUnit/selftest, ни `dtbs_check`, ни compile-gate в скриптах: текущий HEAD с некомпилируемым ringfb это подтверждает. На железе ничего не проверялось; вендорского keypad-бинарника нет (только DTS), поэтому debounce/полярность keypad — предположение. | minor | Добавить сборку трёх объектов в CI/`build-all.sh`; для ringfb — тест парсера label/ключа (можно userspace-модель), для rotary — проверку обеих последовательностей. |

## Соответствует вендору (проверено по бинарникам/DTS)

| Что | Драйвер | Вендор / доказательство |
|---|---|---|
| Канал кольца из label: `(N-1)*3 + c`, c=red/green/blue | `ledtrig-ringfb.c:43-75` | `led_ring_trigger_activate`: `strchr(':')`, сравнение `red:`/`green:`/`blue:`, `kstrtoll` после `ring`, `(n-1)*3+c`; `fb_sync` индексирует `screen_buffer[key]` — LED-major порядок, как в драйвере |
| 24×1, 24 bpp, `line_length=72`, запись 72 байт через `write()` | `:30-36,195-219,247-272` | вендор: те же `var`/`line_length=72`, `screen_size` больше (4096), но userspace пишет 72; `light`/`rotaryd` используют `open(...,"wb").write(72)` |
| trigger `led_ring_fb`, регистрация после fb, отказ при неверном `max_brightness`, deactivate чистит канал | `:123-166,168-193,285-292` | `led_ring_trigger_activate/deactivate`, `led_trigger_register` после `register_framebuffer`; проверка `max_brightness` есть (но значение — 255, см. находку 4) |
| rotary: 2 GPIO, порядок `key-codes`, `00/11` игнор, press+release+SYN, `BUS_HOST=25`, имя `rotary-volume` | `rotary-volume.c:134-145,176-181,91-112` | `rotary_volume_probe` (`ndescs==2`, `of_property_read_variable_u32_array` в `keycodes[0..1]`), `input->name = dev_name` (`rotary-volume`), `bustype=25`, `rotary_volume_report_event` (1,0,SYN) |
| rotary: детент = падение линии при высокой второй; ровно одно событие на щелчок | `:85-101` | `rotary_volume_work`: `state & 3` == 1/2, `00/11` игнор; IRQ `IRQF_TRIGGER_FALLING` (`probe+0x1ac`, `mov x4,#2`) — эквивалентно при корректной таблице (см. находку 3) |
| keypad: `key_num`, `key_code`/`key_name`, GPIOA_15/14, active-low, `scan_period=20`, `detect_mode=0`, polling, без autorepeat | `atrios-keypad.c:172-228,248-266,91-107` | вендор DTS `gpio_keypad` (`key_num=2`, `key_code=<0x246 0xf8>`, `key-gpios=<&gpio 0x41 1 &gpio 0x40 1>`, `scan_period=<20>`, `detect_mode=<0>`); коды 582=KEY_VOICECOMMAND, 248=KEY_MICMUTE |
| keypad: коды 582/248 не перепутаны, `BTN_TRIGGER_HAPPY7` не используется | `:67-70,223-228` | вендор `0x246`/`0xf8`; `KEY_VOICECOMMAND=0x246`, `KEY_MICMUTE=0xf8` в 7.3 |
| rotary IRQ both-edge на SM1 возможен | `:205-210` | `irq-meson-gpio.c`: `sm1_params.support_edge_both=true`, AO `a9_ao` тоже; `GPIOAO_10`/`GPIOC_7` — разные банки, обе линии могут дать IRQ |
| fb `/dev/fb5` находится по имени, а не по номеру | `:256` (`fix.id="led-ring-fb"`) | `overlay/etc/init.d/S60atrios:15-23` создаёт `/dev/fb5` по `/sys/class/graphics/fb*/name`; драйверу ничего менять не нужно |

## Расхождения осознанные (не считать багами без отдельного решения)

- **rotary:** both-edge IRQ + threaded handler + debounce 5 мс вместо вендорских falling-only + `queue_work` без debounce. Для SM1 оба края поддержаны, задержка 5 мс ниже интервала быстрого вращения; менять не обязательно (но см. инверсию в находке 3).
- **keypad:** `detect_mode` используется как глобальный инвертор полярности, а не как «0 polling / 1 irq» (вендорский исходник недоступен, в DTS Station Max 0); debounce «два одинаковых сэмпла» вместо неизвестного вендорского; подписи `key-names`/`key-codes` — современные алиасы вендорских `key_name`/`key_code`.
- **ringfb:** опрос `screen_buffer` раз в 20 мс вместо вендорского `fb_sync` на записи; буфер 72 байта и нет `fb_mmap`/`fb_pan` (вендор даёт mmap на 4 КиБ); нет backlight (вендорский userspace его не использует); запуск только при 72 каналах.
- **keypad/rotary:** оба `=y`, `remove` только останавливает work/IRQ; input освобождается devres — это корректно.

## Вердикт

**Есть расхождения.** На коммите `8b06882` `ledtrig-ringfb` **не собирается** (blocker), в его error-path — double free; `rotary-volume` инвертирует направление относительно вендора; `max_brightness`/кривая яркости ringfb не совпадают с вендором (200 vs 255 + масштабирование). `atrios-keypad` по кодам, GPIO, периоду и режиму соответствует DTS-реконструкции; полярность `KEY_MICMUTE` нужно сверить с `rotaryd` на железе. Остальное — осознанные/документированные отличия либо minor.
