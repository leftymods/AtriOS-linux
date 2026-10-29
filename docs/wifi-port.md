# Wi-Fi RTL8822ES на mainline 7.3 — stage (b): таблицы и efuse 8822E

Практическая часть порта RTL8822ES (`sdio:c07v024CdA822`, vendor `0x024c`,
device `0xa822`) на Linux 7.3. Досье по реверсу и обоснование стратегии —
в `docs/wifi.md`; источник таблиц — публичный GPL-2.0 вендорский драйвер
(`docs/wifi-analog.md`); здесь только то, что реально сделано, как
собрать и что проверять на железе.

Stage (a) — новый SDIO ID, `rtw8822e_hw_spec` и родная прошивка на всех
8822C-таблицах. Stage (b) — **настоящие таблицы 8822E** (BB/AGC/RF A/B/
cal-init/txpower-limit) и **efuse-парсер 8822E**. Калибровочный код
(IQK/DPK/TSSI, power tracking, DPD) пока остаётся 8822C — см. §7.

---

## 1. Что изменено

Два патча, применяются по порядку (0011 **требует** 0010):

| Патч | Строк | Что делает |
|---|---|---|
| `0010-rtw88-8822e.patch` | 255 | `SDIO_DEVICE_ID_REALTEK_RTW8822ES 0xa822`, `rtw8822e_hw_spec` (stage a), модуль `rtw88_8822es`, Kconfig/Makefile |
| `0011-rtw88-8822e-tables.patch` | ~17 400 / 462 КБ | `rtw8822e_table.c/.h` (все таблицы 8822E), `rtw8822e_efuse` + `rtw8822e_read_efuse`, `rtw8822e_ops`, `rtw8822e_rfe_defs`, подключение в `rtw8822e_hw_spec` |

Файлы 0011:

| Файл | Что добавлено |
|---|---|
| `rtw88/rtw8822e_table.c` | новый, 17 105 строк / 432 КБ: данные + парсер таблиц |
| `rtw88/rtw8822e_table.h` | новый: объявления `rtw8822e_*_tbl` |
| `rtw88/rtw8822c.h` | `struct rtw8822e_efuse` |
| `rtw88/rtw8822c.c` | `rtw8822e_read_efuse`, `rtw8822e_ops`, `rtw8822e_rfe_defs`; у `rtw8822e_hw_spec` заменены `.agc_tbl/.bb_tbl/.rfk_init_tbl/.rf_tbl/.rfe_defs/.ops` |
| `rtw88/Makefile` | `rtw8822e_table.o` в `rtw88_8822c-objs` |

Применение:

```sh
cd /home/leftymods/mainline-src/linux-7.3
patch --batch --forward -p1 < .../patches/linux/0010-rtw88-8822e.patch
patch --batch --forward -p1 < .../patches/linux/0011-rtw88-8822e-tables.patch
# или scripts/apply.sh --kernel-only (оба патча подхватятся по имени)
```

`0011` проверен `patch --dry-run` на состоянии «0010 применён»: 5 файлов,
чисто.

Конфиг: нужен `CONFIG_RTW88_8822ES=m` (плюс уже существующие
`CONFIG_RTW88=m`, `CONFIG_RTW88_8822C=m`, `CONFIG_RTW88_SDIO=m`,
`CONFIG_MMC=y/m`). В `configs/atrios.config` этой строки пока
нет — это отдельная правка (файл не входил в зону задачи):

```diff
 CONFIG_RTW88=m
 CONFIG_RTW88_8822CS=m
+CONFIG_RTW88_8822ES=m
```

Для проверочной сборки опция включена в `out/linux/build/.config`
(`scripts/config --module RTW88_8822ES` + `make olddefconfig syncconfig`).

---

## 2. Таблицы 8822E

Источник — `github.com/Cohen0415/rtl8822cs-driver`, дерево `rtl8822cs/`,
дроп v5.15.0.1-36, GPL-2.0:

| Файл | Массив |
|---|---|
| `hal/phydm/rtl8822e/halhwimg8822e_bb.c` | `array_mp_8822e_agc_tab`, `array_mp_8822e_phy_reg`, `array_mp_8822e_phy_reg_pg` |
| `hal/phydm/halrf/rtl8822e/halhwimg8822e_rf.c` | `array_mp_8822e_radioa`, `_radiob`, `_txpwr_lmt`, `_txpwr_lmt_type5` |
| `hal/phydm/halrf/rtl8822e/halrf_rfk_init_8822e.h` | `array_mp_8822e_cal_init` |

Конвертер (вне дерева ядра):
`tools/rtw88-8822e-tables.py`.

```sh
python3 tools/rtw88-8822e-tables.py --src /tmp/wifi-drop \
        --c <kernel>/drivers/net/wireless/realtek/rtw88/rtw8822e_table.c \
        --h <kernel>/drivers/net/wireless/realtek/rtw88/rtw8822e_table.h
```

### 2.1 Что перенесено

| Таблица | Символ | Объём | Байт в образе |
|---|---|---|---|
| AGC | `rtw8822e_agc` | 7 978 u32 | 31 912 |
| BB (digital) | `rtw8822e_bb` | 2 600 u32 | 10 400 |
| BB-PG | `rtw8822e_bb_pg_type0` | 46 строк × 6 u32 | 1 104 |
| RF path A | `rtw8822e_rf_a` | 6 334 u32 | 25 336 |
| RF path B | `rtw8822e_rf_b` | 6 902 u32 | 27 608 |
| cal-init (IQK/DPK-подготовка) | `rtw8822e_array_mp_cal_init` | 5 222 u32 | 20 888 |
| txpower limit type0 | `rtw8822e_txpwr_lmt_type0` | 1 488 строк | 11 904 |
| txpower limit type5 | `rtw8822e_txpwr_lmt_type5` | 868 строк | 6 944 |

Итого `rtw8822e_table.o`: `.text` = 132 480 байт; `rtw88_8822c.ko`
вырос с 858 896 до **1 053 936** байт (+195 КБ). Цена: таблицы 8822E
теперь лежат в общем `rtw88_8822c.ko`, т.е. их грузят и 8822CE/CS/CU.

### 2.2 Почему данные оставлены «как в вендоре»

Вендорские PHY-массивы — плоские пары `u32` с двумя служебными слоями:

1. **Headline** (пары с верхним нибблом `0xf`): выбор варианта таблицы по
   `(cut_version, rfe_type)`. Вендор перебирает: точное совпадение →
   `cut` don't-care → наибольший `cut` при совпавшем `rfe` → наибольший
   `cut` при `rfe=0xff` → отказ.
2. **Маркеры в теле**: `0x8xxxxxxx` IF, `0x9xxxxxxx` ELSE_IF,
   `0xA0000000` ELSE, `0xB0000000` ENDIF, `0x4xxxxxxx` CHK
   (сравнение `cfg_para` с выбранным headline).

`rtw_parse_tbl_phy_cond()` из rtw88 так не умеет: у него `struct
rtw_phy_cond` — простое И/ИЛИ по полям `cut/pkg/intf/rfe`, без headline
и без «наибольшего cut». Преобразовать headline в конкретные `cut/rfe`, как
это сделано для 8822C, значило бы угадывать ревизию чипа. Поэтому:

* массивы сохранены **дословно** (включая headline и маркеры);
* в `rtw8822e_table.c` добавлен `rtw8822e_parse_tbl_phy_cond()` — порт
  вендорского `halbb_sel_headline()` + `odm_read_and_config_mp_8822e_*()`
  один-в-один.

Проверено: сгенерированные массивы совпадают с исходником по значениям
(все 7 таблиц, сравнение скриптом), txpower — с учётом отбрасывания 2T.

### 2.3 txpower limit: потерянное измерение 2T

В вендорской таблице есть поле `ntx` (1T/2T), и для 2T лимиты обычно ниже
(например ETSI 2.4G CCK ch1: 1T=60, 2T=48). В rtw88
`struct rtw_txpwr_lmt_cfg_pair` измерения по числу антенн нет; upstream
8822C-таблицы тоже оставляют значения 1T. Сделано так же: 2T-строки
отброшены (1488 из 2976 в type0, 868 из 1736 в type5). Это осознанное
упрощение, а не ошибка конвертации.

### 2.4 Регуляторные домены

Символьные `PW_LMT_REGU_*` отображены в `enum rtw_regulatory_domains`:
FCC→0, MKK→1, ETSI→2, IC→3, KCC→4, ACMA→5, CHILE→6, UKRAINE→7,
MEXICO→8, CN→9, QATAR→10, UK→11. `PW_LMT_REGU_WW13` в таблицах 8822E
не встречается. Проверка: первые строки type0 дают ровно те же числа,
что 8822C-таблица rtw88 (72/60/68/72/76/60/…).

### 2.5 rfe_defs

`rtw88` падает в probe, если `efuse.rfe_option >= rfe_defs_size`. Чтобы
неизвестный efuse не валил probe, `rtw8822e_rfe_defs[]` покрывает весь
8-битный диапазон (`[0 ... 255]`), для `rfe=5` берётся type5-таблица
(как у 8822C), для остальных — type0. На выбор ветки PHY-таблиц это не
влияет: `rtw8822e_parse_tbl_phy_cond()` читает реальный
`efuse.rfe_option` сам.

---

## 3. EFUSE 8822E

Вендорская карта — `rtl8822cs/include/hal_pg.h`,
`EEPROM_*_8822E` / `EEPROM_MAC_ADDR_8822ES`. Существенно: **для всех
полей, которые читает rtw88, смещения совпадают с 8822C**, включая
`rfe_option` @ **0xca** (а не 0xc7 — это была ошибка early-досье) и
`country_code` @ 0xcb. Отличия от `struct rtw8822c_efuse`:

* `0xbc..0xc0` документированы как PA type / LNA type (2G AB/CD, 5G
  AB/CD), а не `reserved`; rtw88 их не использует, но в
  `struct rtw8822e_efuse` они названы;
* MAC для SDIO — @ **0x16a** (`EEPROM_MAC_ADDR_8822ES`), как у 8822CS.

Реализация:

* `struct rtw8822e_efuse` (`rtw8822c.h`) + `rtw8822e_read_efuse()`
  (`rtw8822c.c`);
* в `rtw8822e_read_efuse()` стоят `BUILD_BUG_ON` на смещения
  `rfe_option=0xca`, `country_code=0xcb`, `path_a_thermal=0xd0`,
  `mac_addr=0x16a` — если структура поедет, сборка упадёт, а не
  прочитает мусор;
* читаются: `rfe_option`, `rf_board_option`, `channel_plan`, `xtal_k`
  (`crystal_cap`), `country_code`, `rf_bt_setting`, `tx_pwr_calibrate_rate`
  (`power_track_type`), thermal A/B, `txpwr_idx_table[4]`, MAC;
* `rtw8822e_ops` — копия `rtw8822c_ops` с заменой только
  `.read_efuse`.

Ограничения (не проверено на железе):

* `phy_efuse_size/log_efuse_size/ptct_efuse_size` оставлены 512/768/124.
  У вендора для 8822E `EFUSE_MAX_MAP_LEN=2048`, `PG_VER_LEN=7`, а HALMAC
  ограничивает логическое чтение 416 байтами. 768 покрывает все поля,
  которые мы читаем (максимум 0x16a+6 = 0x170), но реальный размер
  логической карты и поведение `rtw_dump_logical_efuse_map()` на 8822E
  надо подтвердить дампом.
* PA/LNA type (0xbc..0xc0) не используются: DPDT/antdiv остаются на
  дефолтах 8822C.

---

## 4. Прошивка

Blob лежит внутри вендорского `88x2es.ko` (`array_mp_8822e_fw_nic`,
файловое смещение `0x256208`, размер `0x30b90`):

```sh
dd if=88x2es.ko of=rtw8822e_fw.bin bs=1 skip=$((0x256208)) count=$((0x30b90)) status=none
```

| Свойство | Наш blob (из `.ko`, -17) | `array_mp_8822e_fw_nic` в исходнике (-36) |
|---|---|---|
| Размер | **199 568** | **199 640** (+72) |
| sha256 | `d82bf903d63b565f51c253bece9ad4b33d7268a8e7aa32d590c6c9fa0e298aef` | `edb07476f4f7e598e3341c508e87deffb1ed53e37a949bcf6905da4916d09ab7` |
| `version.subversion.subindex` | 1.0.**7** | 1.0.**10** (`0x0a`) |
| `dmem_addr/size` | `0x80200000` / `0x4690` | то же |
| `imem_size` | `0xD7D8` | то же |
| `emem_size` | `0x1ECD0` | `0x1ED18` (+0x48) |
| `h2c_fmt_ver` | 15 | 15 |

Обе раскладки проходят `check_firmware_size()` (`mac.c`):
`64 + (0x4690+8) + (0xD7D8+8) + (emem+8)` = 199 568 / 199 640.
**Решение: оставлена наша прошивка 1.0.7** — она соответствует
установленному `88x2es.ko` (-17) и уже проверена по размеру/заголовку.
Версия из исходника (-36) новее на 72 байта (другой `emem_size`), её
имеет смысл попробовать только если 1.0.7 не заведётся; менять blob
«на всякий случай» не стали.

Файл в репозитории: `firmware/rtw88/rtw8822e_fw.bin`.

Установка на целевую систему (rootfs образа):

```sh
install -Dm644 firmware/rtw88/rtw8822e_fw.bin \
        <rootfs>/lib/firmware/rtw88/rtw8822e_fw.bin
```

Проверка:

```sh
sha256sum /lib/firmware/rtw88/rtw8822e_fw.bin   # d82bf903...
```

`rtw8822c_wow_fw.bin` для 8822ES не нужен (WoW выключен), но безвреден.

---

## 5. Сборка (проверено)

Дерево `linux-7.3`, build-dir `out/linux/build`, кросс-компилятор
`aarch64-linux-gnu-gcc 15.2.0`. Полная сборка ядра не запускалась — только
подкаталог rtw88:

```sh
S=/home/leftymods/mainline-src/linux-7.3
B=/home/leftymods/atrios-out/linux/build

$S/scripts/config --file "$B/.config" --module RTW88_8822ES
make -C "$S" O="$B" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make -C "$S" O="$B" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- syncconfig
make -C "$S" O="$B" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     M="$S/drivers/net/wireless/realtek/rtw88" -j16 modules
```

Результат: **ошибок и предупреждений нет**. Собраны:

```
rtw88_core.ko  rtw88_sdio.ko  rtw88_8822c.ko  rtw88_8822cs.ko  rtw88_8822es.ko
```

| Модуль | Размер | sha256 |
|---|---|---|
| `rtw88_8822c.ko` | 1 053 936 | `9c436df6867d8334b73a3ae3be26b103795eeb5c3983b170d3982e9a231b0fad` |
| `rtw88_8822es.ko` | 57 160 | `112a1840c491da2022d4f8e1681842755446c48b5a6d3caa1f0a507e716f0e67` |
| `rtw88_8822cs.ko` | 57 008 | `85b8ea9f38f9d7c2db71dd4a7022e6cee205c5f3d2532d2eda42cd0b9ee5ac36` |

`rtw88_8822es.ko`: `depends=rtw88_sdio,rtw88_8822c`,
`alias=sdio:c*v024CdA822*`, `vermagic=7.3.0-atrios … aarch64`,
firmware `rtw88/rtw8822e_fw.bin`. Символы `rtw8822e_hw_spec`
экспортируется из `rtw88_8822c.ko`; таблицы `rtw8822e_*_tbl` — в нём же.

> `make O=... M=... modules` для каталога внутри дерева пишет объектники в
> сам каталог исходников, а не в `O=`; после проверки `.ko` скопированы в
> `out/linux/build/drivers/net/wireless/realtek/rtw88/`.
> Собирать `make drivers/.../rtw88_8822es.ko` не надо (падает modpost).

Установка: скопировать **весь** пересобранный набор rtw88 (новый
`rtw88_8822c.ko` обязателен — в нём таблицы и efuse-парсер), затем
`depmod -a`. Без нового `rtw88_8822c.ko` будет `Unknown symbol
rtw8822e_hw_spec`.

---

## 6. Что проверено статически

| Проверка | Результат |
|---|---|
| 0011 применяется поверх 0010 (`patch --dry-run -p1`) | чисто, 5 файлов |
| Сборка rtw88 | ok, 0 warning/error |
| Данные таблиц == вендорский исходник | да, все 7 массивов (сравнение скриптом) |
| `rtw8822e_table.o` `.text` | 132 480 B |
| Символы в `rtw88_8822c.ko` | `rtw8822e_{bb,agc,rf_a,rf_b,array_mp_cal_init,txpwr_lmt_type0/5,bb_pg_type0}_tbl`, `rtw8822e_read_efuse` |
| Автономный тест парсера (stub `do_cfg`) | rfe 1..4: BB 1293, AGC 642, RF A 870, RF B 848 записей; rfe 0x15/0x16/0xff: RF A/B — 0 записей + warning (см. §7) |
| Efuse offsets | `BUILD_BUG_ON` на 0xca/0xcb/0xd0/0x16a проходит |
| Firmware size-чек | 199 568 == файлу; 1.0.10 из исходника 199 640 |
| `MODULE_DEVICE_TABLE` | `sdio:c*v024CdA822*` |

Важное наблюдение из теста парсера: вендорские RF-таблицы **не содержат**
ветки для `rfe 0x15/0x16` (21/22) и для `rfe=0xff`, хотя headline такие
варианты объявляет. Для этих значений RF A/B не пишут ничего — так ведёт
себя и вендорский драйвер (ELSE + отказ). Поэтому на железе первым делом
надо увидеть в dmesg реальный `rfe_option` (ожидаемо 1..4) и отсутствие
нашего warning'а.

---

## 7. Что осталось 8822C (осознанно)

| Область | Сейчас | Риск |
|---|---|---|
| IQK/DPK/TSSI/TXGAPK/spur | код `rtw8822c_*`, но с 8822E `cal_init` | Главный риск. Последовательность инициализации 8822E подаётся в 8822C-алгоритмы; возможны мусорные коэффициенты, таймауты, `-EBUSY` в `iw dev info`. Нужен форк `rtw8822e.c` (stage c) |
| DPD | `en_dis_dpd=true`, `dpd_ratemask=DIS_DPD_RATEALL` (8822C DPK) | Вендор для rfe 21/22 принудительно обходит DPK; у нас DPK включён. Если RF-калибровка не сойдётся — первым делом `dpd_ratemask = 0` / отключить DPK |
| Power tracking (delta swing) | `rtw8822c_pwr_track_type0_tbl` | 8822E-массивы `delta_swingidx_*_8822e` в исходнике есть, но не перенесены; возможен уход мощности от температуры |
| SDIO intf/timing | `phy_para_table_8822c` (`intf_table`) | Тайминги 8822E не извлечены; ошибки CMD53 на высокой частоте. Проверять 100 → 50/25 МГц |
| Power on/off, page/rqpn, digital MAC init | 8822C | По досье совпадают; риск низкий |
| WoW / sched scan | выключены | Нет 8822E WoW-прошивки |
| `.id = RTW_CHIP_TYPE_8822C` | 8822C | Нужно для SDIO-квирков; менять только вместе с sdio.c |
| Логический efuse 768 | 8822C-значение | Может быть мало для полной карты 8822E; читаемые поля < 0x170 |
| 2T txpower limit | отброшен | Лимиты для 2 антенн могут быть выше регуляторных (как и в upstream 8822C) |

---

## 8. Проверка на железе

Порядок (DTS-узел `sd_emmc_a` — см. `docs/wifi.md` §1.1–1.2, DTS в эти
патчи не входит):

```sh
# 1. Карта и драйвер
ls -l /sys/bus/sdio/devices/
cat /sys/bus/sdio/devices/*/modalias          # sdio:c07v024CdA822
dmesg | grep -iE 'mmc[0-9]|sdio|rtw88|rtw8822e'

# 2. Прошивка
dmesg | grep -iE 'firmware|fw version|download'
#   ожидается: Firmware version 1.0.7, H2C version 15

# 3. EFUSE / RFE — ключевая проверка stage (b)
dmesg | grep -iE 'use rfe_def|rfe .* isn.t supported|no phy entries'
#   ожидается "use rfe_def[N]" c N=1..4 и НЕТ "no phy entries applied".
#   Если N=21/22/255 -> RF-таблицы не применятся (вендорское поведение).

# 4. Регистры/таблицы (CONFIG_RTW88_DEBUGFS=y)
ls /sys/kernel/debug/ieee80211/phy0/rtw88/
cat /sys/kernel/debug/ieee80211/phy0/rtw88/phy_info
cat /sys/kernel/debug/ieee80211/phy0/rtw88/tx_pwr_tbl | head
cat /sys/kernel/debug/ieee80211/phy0/rtw88/rf_dump

# 5. Сеть
ip link show wlan0
iw dev wlan0 info
iw dev wlan0 scan | head -40
iw reg get
# затем ассоциация и трафик:
wpa_supplicant -i wlan0 -c ...   # или NetworkManager
ping -I wlan0 <gw>
```

Дополнительно полезно:

* `CONFIG_RTW88_DEBUG=y` + `dmesg -w` в момент probe;
* сравнить MAC из efuse с наклейкой/вендором;
* если RF не инициализируется — попробовать 50 МГц (`max-frequency` в DTS),
  затем 25 МГц;
* 2.4 ГГц TX у devourer под вендорским ядром не декодировался — проверять
  отдельно, не считать регрессией mainline.

---

## 9. Если stage (b) не завелась

| Симптом в dmesg | Вероятная причина | Действие |
|---|---|---|
| `use rfe_def[21]` / `[22]` / `[255]` + `no phy entries applied` | efuse отдаёт неожиданный rfe; RF-таблицы не применились | снять дамп efuse (`/proc/net/rtl88x2es/*efuse*` на вендорском ядре), сверить 0xca; при необходимости добавить ветку/учесть в парсере |
| probe ok, `wlan0` есть, scan пусто | RF/AGC не применились или калибровка 8822C | проверить warning'и парсера, `rf_dump`; stage (c) — 8822E IQK/DPK/TSSI |
| `-EBUSY` в `iw dev info`, IQK/TSSI-ошибки | калибровки 8822C на 8822E cal-init | stage (c), временно отключить DPK (`dpd_ratemask=0`) |
| слабый/кривой линк | efuse/thermal/txpwr или power tracking 8822C | сверить rfe/MAC/thermal, затем переносить delta swing 8822E |
| `failed to download firmware` | прошивка/питание/32k/SDIO-тайминги | проверить blob, 50 МГц, вендорский лог |
| `Unknown symbol rtw8822e_hw_spec` | не установлен новый `rtw88_8822c.ko` | `depmod -a`, переустановить весь набор rtw88 |

Stage (c) = форк калибровок: `rtw8822e.c` (IQK/DPK/TSSI/TXGAPK/spur/
power-track из вендорских `halrf_*_8822e.c`), перенос
`delta_swingidx_*_8822e`, `phy_para_table` 8822E, DPD-политика по rfe,
возможно `rtw8822e.h`. Точка входа — тот же `rtw8822e_hw_spec`: заменить
`.ops` и `.pwr_track`/`.rfe_defs`.

---

## 10. Откат

```sh
cd /home/leftymods/mainline-src/linux-7.3
patch -R -p1 < .../patches/linux/0011-rtw88-8822e-tables.patch
patch -R -p1 < .../patches/linux/0010-rtw88-8822e.patch
# опционально: rm /lib/firmware/rtw88/rtw8822e_fw.bin
```

После отката 0011 8822ES возвращается к stage (a) (8822C-таблицы), после
отката 0010 — `0xa822` перестаёт матчиться. Работа 8822CS/CE/CU не
затрагивается ни в одном из состояний.
