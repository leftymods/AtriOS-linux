# Ревью ASoC-кодеков es7210 / es8156 / sy6045s

Дата: 2026-10-29, ветка `mainline` (worktree `/home/leftymods/atrios-mainline`).
Ревьюер: независимая проверка, код не изменялся; отчёт — единственный
созданный файл.

Проверялись:

* `drivers/audio/es7210.c` (728 строк)
* `drivers/audio/es8156.c` (641 строка)
* `drivers/audio/sy6045s.c` (1143 строки)

Вендорские эталоны (DWARF, не stripped):

```
board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/
  snd-soc-es7210.ko
  snd-soc-es8156.ko
  snd-soc-sy6045s.ko
```

Метод: `nm -S`, `readelf -SW/-rW/-x`, `pahole -C`, `objdump -dr`,
`strings`, плюс прямое сравнение байтов `.rodata`/`.data` вендорских
модулей с массивами/литералами драйверов (скрипт на Python, разбор
`REL`-секций). Сборка: `make -C linux-7.3 O=out/linux/build ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu- M=... W=1 modules`; `scripts/checkpatch.pl
--no-tree --strict -f`; `modinfo`.

Итоговый вердикт: **регистровая карта и TLV в основном совпадают с
вендором, но есть один критичный дефект контролов sy6045s (инверсия
Playback Switch), из-за которого штатный userspace глушит усилитель.
Плюс несколько расхождений в неиспользуемых на этой плате режимах
(set_fmt/hw_params), сужение форматов sy6045s и гонки workqueue.**

## 1. Находки по убыванию серьёзности

| файл:строка | проблема | серьёзность | исправление |
|---|---|---|---|
| `drivers/audio/sy6045s.c:860-862` | Три контрола `Master/Ch1/Ch2 Playback Switch` объявлены `SOC_SINGLE(..., 0)` (invert=0). В вендорском `sy6045s_snd_controls` (`.rodata+0x390`) у всех трёх `soc_mixer_control` стоит `invert=1`: литералы `.data+0x540` (Master, shift 3), `.data+0x4d8` (Ch1, shift 0), `.data+0x470` (Ch2, shift 1). Биты 0/1/3 регистра 0x06 — это mute: в `sy6045s-tweeters-settings.txt:49` `w 54 06 08` подписан `#mute and enable I2C access to EQ`, а `:153` `w 54 06 00` — `# Unmute`. При invert=0 `amixer ... cset 'Woofer Master Playback Switch' 1` (так делает `/usr/lib/atrios/audio:55-56`, и `asound.state` хранит `value true`) пишет 1 в mute-бит, т.е. **выключает звук**. | **Critical** | Передать `1` пятым аргументом: `SOC_SINGLE("Master Playback Switch", SY6045S_REG_SWITCH, 3, 1, 1)` и аналогично Ch1/Ch2. |
| `drivers/audio/sy6045s.c:788` | DAI рекламирует только `SNDRV_PCM_FMTBIT_S32_LE`. Вендорский `sy6045s_dai` (`.data+0xd8`) имеет `formats = 0x444` = `S16_LE｜S24_LE｜S32_LE`; `hw_params` драйвера (строки 717-726) умеет S16/S24, но эти ветки недостижимы. Плейбек S16/S24 через `hw:`/`default` без plug падает. | **High** (зависит от userspace) | Вернуть `S16_LE｜S24_LE｜S32_LE` (0x444), как у вендора. |
| `drivers/audio/sy6045s.c:744-765`, `650-666` | `spk_on_work` и `spk_off_work` — два разных work на `system_highpri_wq`. На быстром STOP→START (или START→STOP) они могут выполняться одновременно/в обратном порядке; последняя запись бита 4 регистра 0x22 недетерминирована → усилитель может остаться включённым после STOP (pop/защита) или выключенным после START. | **Medium** | Один work + atomic «желаемое состояние» (или mutex + флаг), либо `mod_delayed_work`/`flush_work` с сериализацией. Не queue_work на два разных work. |
| `drivers/audio/sy6045s.c:1103-1114` | В `remove`: `cancel_work_sync()` вызывается **до** `snd_soc_unregister_component()`. PCM-trigger ещё может выполняться и заново поставить work после cancel; work выполнится после выключения регуляторов/освобождения `priv` (devres) → I2C-ошибки или use-after-free. | **Medium** | Сначала `snd_soc_unregister_component(dev)` (останавливает PCM-операции), затем `cancel_work_sync()`, затем `sy6045s_set_sby_mode_on()`, затем регуляторы. |
| `drivers/audio/es7210.c:210` | `ES7210_ADC34_HPF2` в `es7210_init_regs[]` = `0x26`; в вендорской `es7210_tdm_reg_common_cfg` (`.rodata+0x190`, 21 запись) по адресу 0x20 лежит **0x06** (остальные 20 записей совпадают байт-в-байт). Это меняет коэффициент HPF ADC3/4 (2 из 4 микрофонов). | **Medium** | `{ ES7210_ADC34_HPF2, 0x06 }`. |
| `drivers/audio/es8156.c:313-315` | `set_fmt` отображает `SND_SOC_DAIFMT_RIGHT_J` → 1 и отвергает `LEFT_J` (default → -EINVAL). Вендорский `es8156_set_dai_fmt` (`.text+0x3c8`) отображает **LEFT_J (fmt=3) → 1** и отвергает RIGHT_J (fmt=2); значения 4/5 → 3/7. На плате используется I2S, поэтому дефект латентный. | **Medium/Low** | Переименовать константу в `ES8156_SDP_FMT_LEFT_J` и мапить `LEFT_J` → 1; `RIGHT_J` — -EINVAL. |
| `drivers/audio/sy6045s.c:693-703` | Маппинг инверсии в `hw_params` сдвинут относительно вендора. Вендор (`sy6045s_hw_params`, `.text+0x418`): `0x200` (NB_IF) → 0x20, `0x300` (IB_NF) → 0x40, `0x400` (IB_IF) → 0x60, иначе 0. Код: `IB_NF` (0x300) → 0x20, `IB_IF` (0x400) → 0x40, `NB_IF` (0x200) → 0. На плате `NB_NF` (0), поэтому дефект латентный. | **Medium/Low** | Привести case-ы к значениям вендора (NB_IF/IB_NF/IB_IF → 0x20/0x40/0x60). |
| `drivers/audio/es7210.c:479-493` | Инверсия в `MODE_CFG` не совпадает с вендором. Вендор (`es7210_set_dai_fmt`, `.text+0x180`) ставит bit3 для `NB_IF` (0x200) и `IB_IF` (0x400), снимает для `NB_NF`/`IB_NF`. Код ставит bit3 для `IB_NF`/`IB_IF`. Судя по поведению вендора, bit3 — это, скорее, инверсия **frame/FSYNC**, а не BCLK; комментарий `ES7210_MODE_BCLK_INV` и фраза «NB_IF behaves like NB_NF» неверны. Плата использует NB_NF, поэтому латентно. | **Low** | Мапить NB_IF/IB_IF → bit3, NB_NF/IB_NF → 0; переименовать бит в FRAME_INV (или подтвердить по даташиту). |
| `drivers/audio/es7210.c:425-436` | `CBP_CFP` трактуется как frame master (bit0). Вендор ставит bit0 только для `CBC_CFP` (0x2000); для `CBP_CFP` (0x1000) бит не ставится. Плата — `CBC_CFC`, латентно. | **Low** | Либо повторить вендор (master только CBC_CFP), либо задокументировать осознанное исправление. |
| `drivers/audio/sy6045s.c:1083-1094` | Компонент регистрируется (строка 1071) раньше загрузки settings и программирования BTL/PBTL/SPK-off. Если карта привяжется и userspace успеет открыть PCM/контролы в этом окне, усилитель ещё не сконфигурирован. Вендор тоже регистрировал кодек раньше, но грузил fw в component probe; тем не менее порядок стоит сделать безопасным. | **Low/Medium** | Загрузить fw и запрограммировать BTL/PBTL/SPK-off **до** `snd_soc_register_component()`. |
| `drivers/audio/sy6045s.c:826-830` | `snd_soc_component_read()` возвращает `int`; отрицательный код ошибки присваивается `unsigned int val`, затем `(val & mask) != off_value` → при ошибке I2C контрол рапортует «on» и не возвращает ошибку. | **Low** | Проверять `ret < 0` и возвращать `ret`; только потом сравнивать. |
| `drivers/audio/sy6045s.c:1052-1056` | `reset_gpio` запрашивается как `GPIOD_OUT_HIGH`, затем после 20 мс ставится `1` — перехода нет, импульса сброса нет. Если линия active-low reset, чип остаётся в сбросе. Вендор делал то же самое; на плате свойство не используется. | **Low (латентно)** | После задержки ставить `0` (импульс как в es7210) либо явно описать полярность/семантику (enable, не reset). |
| `drivers/audio/sy6045s.c:938-940`, `965-967` | Результат `sy6045s_restore_regs()` игнорируется: неудачное восстановление регистров после reset не логируется и sysfs возвращает успех. | **Low** | Проверять/логировать ошибку restore; при ошибке возвращать её из sysfs-store. |
| `drivers/audio/sy6045s.c:381-388` | Парсер `>` проверяет только длину строки; `>x11` (без пробела) принимается, т.к. `sy6045s_hexbyte(&buf[pos+2])` читает `11`. Вендорский формат — `> <byte>`. | **Low** | Проверять `buf[pos+1] == ' '` (и что `buf[pos+2]`/`[pos+3]` — hex). |
| `drivers/audio/sy6045s.c:791` | Нет `.symmetric_rate = 1`; вендорский `sy6045s_dai` имеет `symmetric_rates = 1`. Влияние низкое (playback-only). | **Low** | Добавить `.symmetric_rate = 1`. |
| `docs/codec-sy6045s.md:197-199` | Таблица контролов не указывает `invert=1` для трёх Playback Switch; из-за этого дефект кода не виден при сверке с документом. | **Doc** | Указать invert=1 (или «1 = unmute, 0 = mute»). |
| `docs/codec-sy6045s.md:249-252` | Утверждение, что «Linux 7.3 изменил константы инверсии на IB_NF=0x300, IB_IF=0x400» неверно: в 4.9 (и в заголовках vendor) уже `NB_IF=0x200`, `IB_NF=0x300`, `IB_IF=0x400`; вендор мапит 0x200→0x20, 0x300→0x40, 0x400→0x60. Отсюда и ошибка в коде. | **Doc** | Исправить таблицу и обоснование. |
| `docs/codec-sy6045s.md:90` | Утверждается, что вендорский `regmap_config.cache_type = REGCACHE_NONE`; в бинарнике `.data+0` `cache_type = 1` (`REGCACHE_RBTREE`). Mainline использует NONE — это безопаснее для write-only/custom read, но расхождение надо описать как осознанное. | **Doc/Low** | Исправить на RBTREE и указать, почему mainline выбрал NONE. |
| `docs/codec-sy6045s.md:218-220` | Вендорский DAI, кроме playback, имеет и capture-стрим (`stream_name = "Capture"`, formats 0x444, rates 0x4e0, 1..2 ch). Mainline портирует только playback. Похоже на артефакт вендора (усилитель не capture-устройство), но это не отражено. | **Doc/Low** | Записать как осознанно не портированный вендорский артефакт. |
| `docs/codec-es7210.md:118` | Для 0x20 (ADC34_HPF2) в колонке TDM init указано 0x26; в бинарнике 0x06 (см. находку по `es7210.c:210`). | **Doc** | Исправить на 0x06. |
| `docs/codec-es7210.md:237` | `rates = 0x7fe` — это не 11025..176400, а 8000..96000 Гц (биты 1..10). Mainline сужает до 8..48 кГц осознанно, но обоснование в тексте неверное. | **Doc** | Исправить диапазон на 8..96 кГц. |
| `docs/codec-es8156.md:206,209` | Таблица set_fmt перепутана: вендор принимает LEFT_J (fmt=3) → 1 и отвергает RIGHT_J (fmt=2), а не наоборот. | **Doc** | Поменять строки LEFT_J/RIGHT_J. |
| `docs/audio.md:217` | Список контролов sy6045s не упоминает инверсию switch-ей; см. критичную находку. | **Doc** | Добавить пометку о полярности. |

## 2. Что соответствует вендору (проверено по бинарнику)

### ES7210

* `es7210_reg_defaults[]` — **52/52** записи совпадают с `.rodata+0x118`
  байт-в-байт.
* `es7210_tdm_reg_common_cfg[]` — 21 запись, **20/21** совпадают;
  единственное расхождение — 0x20 (см. выше).
* TLV: `adc_gain_tlv = {1,8,-9500,50}`, `mic_boost_tlv = {1,8,0,300}`
  (`.rodata+0x838/0x848`) — совпадают.
* 12 контролов: имена, reg/shift/max/invert/TLV/access — совпадают с
  вендорской таблицей `.rodata+0x478` (PGA1..4, ADC1..4_MUTE,
  ADC1..4_MAX_GAIN).
* DAI: capture 2..4 канала, `symmetric_rate=1`; форматы/рейты сужены
  осознанно и описаны (`0x444` без S20_3LE; 8..48 кГц вместо 8..96 кГц).
  Имя DAI `es7210` вместо `ES7210 ADC 2/4CH` — осознанно (требование
  порта, имя нигде в DTS не используется).
* Сброс/инициализация, MIC bias, PGA enable, mute-регистры 0x14/0x15 —
  совпадают.

### ES8156

* 36 записей `es8156_reg_defaults[]` совпадают с `.rodata+0x8f0`; отличие
  только документированное `0x0f = 0x40` (в вендорской таблице 0x10
  продублирован, 0x0f отсутствует).
* Reset, init и power-down последовательности совпадают с
  `es8156_set_bias_level` (`.text+0x58`, проверены immediate-константы).
* TLV `dac_vol_tlv = {1,8,-9550,50}` (`.rodata+0x8e0`) — совпадает;
  raw 191 = 0 dB.
* Контролы `DAC Playback Volume` (0x14, shift 0, max 255, invert 0) и
  `Headphone Switch` (0x11, shift 3, max 1, **invert 1**) — совпадают с
  `.rodata+0x840`.
* `hw_params`: S16→0x30, S24→0x00, S32→0x40 (reg 0x11 bits 6:4) —
  совпадает.
* Master/slave: CBP_CFP → reg 0x02 bit0=1, CBC_CFC → 0 — совпадает.
* Jack detect: 200 мс debounce, оба фронта, `IRQF_ONESHOT`; vendor
  использовал 200 мс. Полярность отдана gpiod-флагам — корректно.
* Имя DAI `es8156` и форматы S16/S24/S32 — осознанно (вендорский
  `.formats` = 0x100000004e не декодируется в разумный набор; в
  документации это отмечено).

### SY6045S

* Таблица ширин `sy6045s_reg_size[256]` — **256/256** байт совпадают с
  `.rodata+0x0`.
* `regmap_config`: reg_bits=8, val_bits=32, max_register=0xff,
  volatile_reg (reg<=2 || 0x0f), кастомные read/write, lock/unlock —
  совпадают (кроме cache_type, см. ниже).
* TLV: `vol_tlv = {1,8,-7950,50|MUTE}`, `vol_tlv_m =
  {1,8,-12750,50|MUTE}` (`.rodata+0x610/0x620`) — совпадают.
* Контролы volume (0x07/0x08/0x09), EQ (0x04 bit4), DRC
  (`{0x60,0x0f,0x0f,0x00}`, `.data+0x3f8`) — совпадают. **Кроме
  инверсии трёх switch-ей.**
* Парсер settings: грамматика, минимум длины строки (`len <= 9` →
  ошибка, как `cmp w4,#9; b.ls`), лимит 32 байта в block write, `>`-блоки,
  комментарии `#`/`d`, CRLF — соответствуют вендорскому
  `sy6045s_apply_settings` (`.text+0xfb0`). MD5 и CRC32 считаются по
  всему файлу; CRC — `~crc32_le(~0, ...)`, как вендор (`.text+0x11e4`).
* sysfs `settings`/`settings_file`/`default_settings`, формат
  `%s %*phN %u\n` (16 байт MD5) — совпадают (`settings_file_show`,
  `.text+0x148`).
* `hw_params` reg 0x15: формат/ширина (кроме INV-маппинга) — совпадают;
  BTL 0x1e=0x05/0x20=0x00 и PBTL 0x1e=0x07/0x20=0x80 — совпадают;
  SPK-off 0x22 bit4 — совпадает.
* DAI name `sy6045s-hifi`, 1..2 канала — совпадают.
* `module_i2c_driver`, `soc_codec_dev_sy6045s` (8 контролов, без DAPM) —
  совпадает с `.data+0x1c0`. Вендорский component probe
  (`sy6045s_probe`, `.text+0x1898`: загрузка settings, BTL/PBTL,
  SPK-off) в порте перенесён в `sy6045s_i2c_probe()`; это объясняет
  отличие в порядке «регистрация компонента vs загрузка fw» (см. §1).

## 3. Осознанные расхождения с вендором (не ошибки, но должны быть в доке)

1. **es7210/es8156 имена DAI** (`es7210`, `es8156` вместо вендорских
   `ES7210 ADC 2/4CH`, `ES8156 HiFi`) — требование порта; в DTS не
   используется имя.
2. **es7210 форматы/рейты**: 0x444 без S20_3LE, 8..48 кГц вместо
   8..96 кГц — сужение под плату, описано.
3. **es8156 форматы/рейты**: S16/S24/S32 и 8..96 кГц; вендорский
   `.formats` — мусорное слово, код следует set_fmt/hw_params, описано.
4. **es7210 не портирован `pcm_pop_work`** (25 jiffies unmute) и
   suspend/resume (`regcache_sync` при `REGCACHE_NONE` — no-op); описано.
5. **es8156 не реализован bias-level** (вендор повторял init на
   STANDBY, что сбрасывало громкость); mainline делает reset+init один
   раз в probe, power-down в component remove; описано.
6. **sy6045s `trigger` использует стандартную семантику**
   (RESUME/PAUSE_RELEASE = on, SUSPEND/PAUSE_PUSH = off), тогда как
   вендор, похоже, перепутал SUSPEND/RESUME; описано.
7. **sy6045s cache_type = REGCACHE_NONE** вместо вендорского RBTREE:
   для write-only/custom-read устройства это безопаснее (нет stale
   cache); в доке ошибочно указано, что и у вендора NONE — поправить
   формулировку.
8. **es7210 cache_type = REGCACHE_RBTREE** вместо вендорского NONE:
   драйвер не читает аппаратные статусы, кэш с `reg_defaults`
   консистентен; можно оставить, но зафиксировать в доке.
9. **sy6045s не портирован вендорский capture-стрим** (вероятный
   copy-paste артефакт усилителя); не влияет на карту.
10. **sy6045s DAI rates 8..96 кГц** шире вендорских 32..96 кГц; не
    ошибка (ограничение придёт от TDM-линка), но лучше привести к
    вендору или обосновать.

## 4. Сборка, checkpatch, modinfo

Сборка каждого модуля (внешний `M=`, тот же `O=out/linux/build`,
`ARCH=arm64`, `CROSS_COMPILE=aarch64-linux-gnu-`, `W=1`) — **успешно,
без предупреждений**; модули линкуются.

* `snd-soc-es7210.ko`: GPL, author AtriOS, `depends` пусто.
* `snd-soc-es8156.ko`: GPL, author AtriOS, `depends` пусто.
* `snd-soc-sy6045s.ko`: GPL, author AtriOS, **`depends: libmd5`**;
  Kconfig `SND_SOC_SY6045S` делает `select CRYPTO_LIB_MD5`, поэтому
  `libmd5.ko` должен быть в образе (modprobe подтянет по depends).

`make ... M=sound/soc/codecs modules` (in-tree) проходит. Отдельная
цель `make ... sound/soc/codecs/snd-soc-sy6045s.ko` **падает** на
`modpost: symbol 'md5' undefined!`, потому что single-target modpost
перегенерирует `Module.symvers` из vmlinux и теряет символы модулей
(`libmd5`); это артефакт команды, а не драйвера. Для проверки
использовать `M=... modules` или полный `make modules`.

checkpatch `--no-tree --strict -f`:

* `es7210.c`: 0 errors, 0 warnings, 0 checks.
* `es8156.c`: 0 errors, 0 warnings, 0 checks.
* `sy6045s.c`: 0 errors, 1 warning (`CONST_STRUCT` на
  `struct regmap_config *config`, строка 991 — неизбежно из-за
  `lock_arg`) и 1 check (`struct mutex io_lock` без комментария, строка
  114). Оба не функциональны.

## 5. Отсутствующие тесты

1. **Парсер settings `sy6045s`** — самый сложный код, но нет ни KUnit, ни
   host-теста. Нужны кейсы: 32/33 байта в block write, `>` без
   предыдущего `w`, обрезанная строка, CRLF, невалидный hex, строка
   ровно 9/10 символов, пустой файл, файл без `\n`, переполнение
   `last_fw_name`.
2. **Сверка контролов/регистров с вендором** — сейчас делается вручную;
   полезен скрипт (как в этом ревью), который парсит `.rodata`/`.data`
   вендорских `.ko` и сравнивает с массивами драйвера. Это поймало бы
   invert-дефект и 0x20.
3. **Тест на инверсию switch-ей** — после исправления проверить, что
   `amixer cset ... 1` пишет 0 в 0x06 (можно на host-модели
   `snd_soc_put_volsw` или на железе через i2cget).
4. **Гонки work** — unit-тест/стресс (быстрые trigger START/STOP) или
   хотя бы code review-чеклист; на железе — осциллограф на 0x22/SPK.
5. **Ничего не проверено на железе**: I2C-записи, MCLK/LRCK,
   mute/pop, 20V rail, jack detect, 4-й микрофон, suspend/resume.

## 6. Не проверить без железа

* Реальное поведение I2C (особенно 20-байтовые block write в firmware
  parser и 3-байтовый DRC 0x60).
* MCLK/LRCK и коэффициенты 0x02 (es7210) / 0x01 (es8156); на плате
  кодеки без `clocks` в DT, MCLK статический.
* Полярность/дребезг `hp-det-gpios` (GPIOX_11) и поведение
  `SW_HEADPHONE_INSERT`.
* 4-й микрофон: DTS-фрагмент использует `everest,es7210` (4ch), но
  TDM-B линк — 2 слота (`dai-tdm-slot-num = <2>`); `es7210_hw_params`
  отвергнет 4 канала при `tdm_slots=2`. Это описано в `audio.md` §8, но
  на железе не проверено; для 4ch нужен 4-слотовый линк или
  `everest,es7210_2ch` для 2ch.
* Влияние HPF-коэффициента 0x20 на ADC3/4 (находка es7210) — только
  измерением АЧХ.
* Реальный эффект инверсии switch-ей sy6045s (логика однозначна, но
  проверить `i2cget` после `amixer` стоит).
* Suspend/resume: ни один драйвер не имеет PM-callback; на этой плате
  рельсы always-on, но если rail выключится, состояние потеряется.
* `reset-gpios` sy6045s (в DTS не используется) — полярность/импульс.

## 7. Рекомендуемый порядок исправлений

1. **sy6045s: invert=1 для трёх Playback Switch** (Critical, одна строка
   каждый).
2. **sy6045s: вернуть S16/S24 в `.formats`** (High).
3. **sy6045s: сериализовать spk on/off work** (Medium).
4. **sy6045s: переставить unregister/cancel_work** (Medium).
5. **es7210: 0x20 = 0x06** (Medium).
6. **es8156: LEFT_J/RIGHT_J** (Medium/Low).
7. **sy6045s: INV-маппинг 0x15** (Low/Medium).
8. **es7210: NB_IF/IB_NF + CBP_CFP** (Low, латентно).
9. Документация: `codec-sy6045s.md` (invert/cache/INV/DAI),
   `codec-es7210.md` (0x20, rates), `codec-es8156.md` (LEFT_J).
10. Тесты: parser KUnit/host, сверка с вендором, стресс trigger.
