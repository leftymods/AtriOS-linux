# Wi-Fi: RTL8822ES (SDIO `024c:a822`)

Досье для порта на mainline (Linux 7.3). Только факты из бинарников/исходников
с указанием `file:line` или символа; предположения помечены явно.

**Короткий вердикт.** RTL8822ES — не «новый» чип, а 8822C-класса кремний с
другим SDIO ID. Доказано: (1) power-on/off последовательности совпадают с 8822C по составу
(те же регистры в том же порядке, то же число шагов, кроме одного шага
в disable); (2) таблица BB-инициализации на ~97 % совпадает с 8822C;
(3) SDIO page/rqpn-таблицы совпадают с 8822C; (4) Wi-Fi прошивка лежит внутри
вендорского модуля и имеет **формат `struct rtw_fw_hdr` из mainline** (размер
сходится до байта). Отличаются: AGC/RF/cal-init/txpwr таблицы, RF-калибровки
(IQK/DPK/TSSI/TXGAPK), раскладка efuse (не проверена).

Отсюда стратегия: **сначала дешёвый эксперимент (a)** — новый SDIO ID +
свой `hw_spec` с прошивкой 8822E; если PHY/калибровки не поедут — **форк
backend (b)**. Вариант (c) (вендорский модуль на 7.3) технически невозможен,
см. §6.

---

## 1. Железо и вендорская обвязка

| Параметр | Значение | Источник |
|---|---|---|
| SDIO alias модуля | `sdio:c07v024CdA822*` → vendor `0x024c`, device `0xa822` | `modinfo 88x2es.ko` |
| Чип | `RTL8822E` / «CHIP TYPE: RTL8822E» | строка в `88x2es.ko` |
| Chip ID внутри драйвера | `0x12` (18) | дизасм `hal_spec_init+0x20` (`cmp w4,#0x12` → `rtl8822e_init_hal_spec`) |
| Драйвер | `88x2es.ko` `v5.15.0.1-17-gd903067ec.20220902_COEX20220722-0000` | `modinfo` |
| Интерфейс | только SDIO (`rtl8822es_xmit_thread`, `rtl8822es_halmac_init_adapter`) | символы |
| BT | отдельный, UART H5 (`btrtl`/`hci_h5`) | `docs/hardware-inventory.md` |

### 1.1 Питание и 32k — это НЕ драйвер

`platform_wifi_power_on()` / `platform_wifi_power_off()` в модуле — **пустышки**
(`mov w0,#0; ret`), дизасм `0x1b4e30` и `0x1b4e4c`. То есть драйвер вообще не
трогает питание: включением чипа управляет Amlogic-драйвер `aml_wifi` через
`/dev/wifi_power` (ioctl), т.е. из userspace/init до `insmod`.

Вендорский DTS (`AtriOS-linux/tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts`):

| Узел | Факт | Строка |
|---|---|---|
| `sdio@ffe03000` (= `sd_emmc_a`) | `bus-width = 4`, `cap-sd-highspeed`, `cap-mmc-highspeed`, `max-frequency = 0x5f5e100` (100 MHz), `disable-wp` | :3084 |
| `sdio@ffe03000/sdio` | `card_type = 3` (SDIO), `ocr_avail = 0x200080`, `max_req_size = 0x20000` (128 KiB), `f_min = 0x61a80` (400 kHz), `f_max = 0xbebc200`, caps = `4_BIT_DATA, MMC/SD_HIGHSPEED, NONREMOVABLE, UHS_SDR12/25/50/104, MMC_PM_KEEP_POWER, SDIO_IRQ` | :3101 |
| `wifi` (`amlogic, aml_wifi`, :3819) | `power_on_pin = <0x18 0x48>` → GPIOX_8, `interrupt_pin = <0x18 0x49>` → GPIOX_9, `irq_trigger_type = "GPIO_IRQ_LOW"` | :3818 |
| `wifi_pwm_conf` | PWM-канал 0: `pwms = <&pwm 0 0x774d 0>`, `duty-cycle = 0x3ba6` → период 30 541 нс ≈ **32.7 кГц**, скважность ≈50 % | :3829 |

Реализация `/dev/wifi_power` — `CONFIG_AMLOGIC_WIFI` → `wifi_dt.o`
(`AtriOS-linux/common/drivers/amlogic/wifi/Makefile:1`), исходников в дереве нет.

**Вывод:** на mainline питание и 32k придётся описать декларативно:
`regulator-fixed` на GPIOX_8 + `mmc-pwrseq-simple` (или свой pwrseq) и решение
по 32k (PWM-канал либо отказ от внешнего LPO). В mainline `rtw88` нет ни одного
обращения к внешнему 32k/LPO (`grep -r '32k\|LPO' rtw88/` → только
`BIT_32K_CAL_TMR_EN` (`reg.h:220`), H2C `SET_NLO_PS_32K` (`fw.h:778`) и комментарий
в `coex.c:1145`), поэтому 32k для 8822C-семейства
mainline считает внутренним/необязательным — **проверить на железе** (см. §8).

### 1.2 В mainline DTS узла Wi-Fi нет вообще

`mainline/dts/meson-sm1-yandex-station-max.dts` содержит `sd_emmc_c` (:91),
`sd_emmc_b` (:104) и **не содержит `sd_emmc_a`** — SDIO-хост для Wi-Fi не
включён. Это отдельный пункт плана (DTS — не моя зона, только фиксирую факт).

---

## 2. Анатомия вендорского `88x2es.ko`

Путь: `AtriOS-linux/board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/88x2es.ko`
Размер 5 028 584 Б, sha256 `f81edba0be134987f907ab51e2f177bf964323e03e26a1cfe04659d2fe0912b7`.

| Свойство | Значение |
|---|---|
| Формат | ELF64 relocatable, AArch64, **not stripped**, 46 секций |
| `.text` | 1 804 876 Б (сумма по функциям 1 671 904 Б) |
| Vermagic | `4.9.y SMP preempt mod_unload modversions aarch64` |
| Зависимости | `cfg80211` (mac80211 — **0** неразрешённых символов; 23 → cfg80211) |
| Символов | 27 208 определённых, 214 неразрешённых |
| Слои | `rtw_halmac_*` 94, `phydm_*` 734, `halrf_*` 202, `odm_*` 99, `rtl8822e*` 79+ |
| Функций с суффиксом `_8822e` | **496**, суммарно 199 016 Б кода |

Важно: это **полный MAC** (свой 802.11 через `cfg80211`, как в
`rtl8xxxu`-классе), а не mac80211-драйвер. Все MLME/очереди/шифрование — внутри
модуля (`rtw_cfg80211_ops`, `rtw_netdev_ops`, `rtl8822es_recv_hdl`,
`rtl8822es_xmit_thread`).

Утёкшие пути сборки (строки в `.rodata.str1.8`) дают структуру вендорского
дерева — полезно для RE:

```
/place/sandbox-data/tasks/3/9/1984047393/quasar/hardware/wifi/realtek/drivers/8822es/rtl88x2ES/
    hal/rtl8822e/rtl8822e_ops.c
    hal/rtl8822e/rtl8822e_phy.c
    hal/rtl8822e/sdio/rtl8822es_halinit.c
    hal/phydm/phydm_rtl8822e.c
    hal/hal8822e_fw.c
```

### 2.1 Слои

| Слой | Что делает | Кол-во символов | Аналог в mainline rtw88 |
|---|---|---|---|
| `rtw_halmac_*` + `halmac_*` | SDIO CMD52/53 доступ, DLFW, C2H, rsvd page, BF, efuse read | 139 | **нет**; размазано по `sdio.c`, `mac.c`, `fw.c` |
| `phydm_*` | PHY DM (dig, EDCCA, AGC, spur, watchdogs) | 734 | `phy.c` + `*_table.c` |
| `halrf_*` | RF-калибровки | 202 | `rtw8822c.c` (IQK/DPK/TSSI/TXGAPK) |
| `rtl8822e*` | chip ops | 79 (T/t); всего символов с `8822e` — 584 | `rtw8822c.c` |
| efuse | `rtl8822e_read_efuse`, `dump_efuse_map_8822e` | ~30 | `rtw8822c_read_efuse` (`rtw8822c.c:44`) |

Типовые HALMAC-функции (доказательство, что это отдельный слой):
`halmac_init_adapter`, `halmac_deinit_adapter`, `rtw_halmac_dlfw`,
`rtw_halmac_dlfw_mem`, `rtw_halmac_cfg_phy_para`, `rtw_halmac_c2h_handle`,
`rtw_halmac_download_rsvd_page`, `rtw_halmac_bf_cfg_sounding`,
`_halmac_sdio_cmd52_read/write`, `_halmac_sdio_reg_read_8/16/32/_n`,
`_halmac_sdio_read_cia`.

### 2.2 Прошивка Wi-Fi — ВНУТРИ модуля (не в `/lib/firmware`)

`hal8822e_fw.c` скомпилирован в модуль: символ
`array_mp_8822e_fw_nic` (`D`, `.data + 0x2a50`, размер `0x30b90` = **199 568 Б**,
файловое смещение `0x256208`).

> ⚠️ Файлы `…/lib/firmware/rtlbt/rtl8822es_fw` и `rtl8822es_config` — это
> **Bluetooth**-прошивка (`RTBTCore`), а не Wi-Fi: они побайтово **идентичны**
> `rtl8822cs_fw`/`rtl8822cs_config` (md5 `70303a988df189c5b0bf25d59f5750e9` /
> `5d9437da141c63a2a275405a768aec41`, по 44 064 / 85 Б). Не путать.

Извлечение (команда воспроизводима):

```sh
dd if=88x2es.ko of=rtw8822e_fw.bin bs=1 skip=$((0x256208)) count=$((0x30b90)) status=none
# sha256: d82bf903d63b565f51c253bece9ad4b33d7268a8e7aa32d590c6c9fa0e298aef
```

Заголовок (разобран по `struct rtw_fw_hdr`, `rtw88/fw.h:316-343`):

| offset | поле | значение |
|---|---|---|
| 0x00 | `signature` | `0x8822` |
| 0x02/0x03 | `category` / `function` | 0 / 0 |
| 0x04 | `version` | **1** |
| 0x06/0x07 | `subversion` / `subindex` | 0 / **7** |
| 0x0C | `feature` | 0 |
| 0x10 | `month,day,hour,min` | 7, 28, 17, 5 |
| 0x14 | `year` | **2022** |
| 0x18 | `mem_usage` | `0x18` (BIT(4)=1 → emem присутствует) |
| 0x1C | `h2c_fmt_ver` | **15** |
| 0x20/0x24 | `dmem_addr` / `dmem_size` | `0x80200000` / `0x4690` |
| 0x30/0x38/0x3C | `imem_size` / `emem_size` / `emem_addr` / `imem_addr` | `0xD7D8` / `0x1ECD0` / `0x80100000` / `0x80030000` |

Контроль сходимости с загрузчиком mainline (`check_firmware_size()`,
`rtw88/mac.c:417-438`, `FW_HDR_SIZE=64`, `FW_HDR_CHKSUM_SIZE=8` — `fw.h:12-13`):

```
64 + (0x4690+8) + (0xD7D8+8) + (0x1ECD0+8) = 199 568  == размеру массива
```

**Т.е. образ валиден для `rtw88` как есть**, порядок секций совпадает с
`start_download_firmware()` (`mac.c:697-740`: dmem → imem → emem).
Подпись `0x8822` в mainline нигде не проверяется (`grep signature` по rtw88
даёт только объявления структур) — загрузчик полностью доверяет заголовку.

### 2.3 Таблицы (`nm -S`, размеры в байтах)

| Символ | Размер | Аналог в `rtw8822c_table.c` |
|---|---|---|
| `array_mp_8822e_phy_reg` (BB init) | 10 400 | `rtw8822c_bb` |
| `array_mp_8822e_phy_reg_pg` (BB PG) | 1 104 | `rtw8822c_bb_pg_type0` |
| `array_mp_8822e_agc_tab` | 31 912 | `rtw8822c_agc` |
| `array_mp_8822e_radioa` | 22 424 | `rtw8822c_rf_a` |
| `array_mp_8822e_radiob` | 25 448 | `rtw8822c_rf_b` |
| `array_mp_8822e_cal_init` | 20 888 | `rtw8822c_array_mp_cal_init` |
| `array_mp_8822e_txpwr_lmt` | 11 904 | `rtw8822c_txpwr_lmt_type0` |
| `array_mp_8822e_txpwr_lmt_type5` | 6 944 | `rtw8822c_txpwr_lmt_type5` |
| `Array_MP_8822E_MSDIO` (+ `_BT`) | 128 (+64) | MP/efuse map |
| `HALMAC_PG_NUM_SDIO_8822E` | 96 | `page_table_8822c` |
| `HALMAC_RQPN_SDIO_8822E` | 168 | `rqpn_table_8822c` |

Формат таблиц — тот же опкод-диалект Realtek (`0xF0FFFFFF`/`0xF1FFFFxx`/
`0x80FFFFFF`/`0x4xxxxxxx` как обёртки условий, затем пары `<addr> <value>`,
например `0x1D90 0x300001BE`), что и в `rtw8822c_table.c` (`rtw8822c_agc`
начинается `0x83000000, 0x0, 0x40000000, 0x0, 0x1D90, 0x300001FF`). Т.е.
конвертация таблиц — механическая, но **значения отличаются** (см. §4.2).

---

## 3. Что есть в mainline 7.3

Проверено по локальному `--filter=blob:none --sparse` клону master:
`65ba8f34ec886ec72b2c07ddc8cf3d815c32869d` (2026-10-27), `Makefile`:
`VERSION=7 PATCHLEVEL=3 SUBLEVEL=0`.

| Проверка | Результат |
|---|---|
| `ls drivers/net/wireless/realtek/rtw88` | 8822b/8822c + `rtw8822ce.c`, `rtw8822cs.c`, `rtw8822cu.c`; 8723b/d/cs, 8703b, 8821a/c, 8812a, 8814a, 88xxa. **8822e нет** |
| `grep -ril 8822e drivers/net/wireless/realtek` | **0 файлов** (ни rtw88, ни rtw89, ни rtlwifi/rtl8xxxu) |
| `grep -rn 'a822' …/realtek` | единственное совпадение — USB PID `7392:a822` в `rtw88/rtw8812au.c:78` (не наш чип) |
| `rtw88/Makefile` | нет `obj-$(CONFIG_RTW88_8822E)`; ID-таблица `rtw8822cs` → `rtw8822c_hw_spec` |
| `include/linux/mmc/sdio_ids.h:129-138` | Realtek: `0xb723, 0xb821, 0xb822, 0xc821, 0xc822, 0xd723, 0xd724, 0xd821, 0xb703`. **`0xa822` отсутствует** |
| rtw89 | **SDIO вообще нет**: `rtw89/Makefile` собирает `pci.o` и `usb.o`, файла `rtw89/sdio.c` нет (404), `grep sdio rtw89/{Makefile,Kconfig}` пусто. Чипы: 8851b, 8852a/b/c/bt, 8922a/d — 8822E не упоминается |

Ключевые места для правок:

| Файл:строка | Что там |
|---|---|
| `rtw88/rtw8822cs.c:12-17` | `SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK, SDIO_DEVICE_ID_REALTEK_RTW8822CS)`, `.driver_data = &rtw8822c_hw_spec` |
| `rtw88/Kconfig:108-117` | `RTW88_8822CS` (depends MMC, select RTW88_SDIO + RTW88_8822C) |
| `rtw88/rtw8822c.c:5339` | `const struct rtw_chip_info rtw8822c_hw_spec` |
| `rtw88/rtw8822c.c:5342` | `.fw_name = "rtw88/rtw8822c_fw.bin"` (+ `:5405` wow_fw) |
| `rtw88/rtw8822c.c:5348-5350` | `phy_efuse_size=512, log_efuse_size=768, ptct_efuse_size=124` |
| `rtw88/rtw8822c.c:5373-5378` | `pwr_on_seq/pwr_off_seq/page_table/rqpn_table/intf_table` |
| `rtw88/rtw8822c.c:4920-4926` | `page_table_8822c[] = {{64,64,64,64,1},…}` |
| `rtw88/sdio.c:1527` | `sdio_set_block_size(func, RTW_SDIO_BLOCK_SIZE)`; `sdio.h:146` → **512** |
| `rtw88/sdio.c:1535` | `sdio3_bus_mode = mmc_card_uhs(...)` |
| `rtw88/sdio.c:951` | `rtw_sdio_enable_rx_aggregation()` (per-chip хуков нет, `dynamic_rx_agg = NULL`) |
| `rtw88/main.h:191-201` | `enum rtw_chip_type` (8822B=0, 8822C=1, …) |
| `rtw88/main.c:1890` | `hal->chip_version = rtw_read32(REG_SYS_CFG1)` (cut, без проверки типа чипа) |
| `rtw88/main.h` | `struct rtw_chip_ops` = **41** колбэк; `rtw8822c_hw_spec` заполняет 99 полей |

---

## 4. 8822E vs 8822C: что доказано

### 4.1 Power-последовательности — идентичны

Массивы в модуле: `TRANS_CARDDIS_TO_CARDEMU_8822E` (0x40 Б), `TRANS_CARDEMU_TO_ACT_8822E`
(0xB0), `TRANS_ACT_TO_CARDEMU_8822E` (0x60), `TRANS_CARDEMU_TO_CARDDIS_8822E` (0x58);
`card_en_flow_8822e` = {carddis→cardemu, cardemu→act, NULL}, `card_dis_flow_8822e` =
{act→cardemu, cardemu→carddis, NULL} — **тот же порядок**, что
`card_enable_flow_8822c` / `card_disable_flow_8822c` (`rtw8822c.c:4858-4868`).

Записи по 8 байт (структура `rtw_pwr_seq_cmd`: `u16 offset, u8 cut, u8 intf,
u8 base:4/cmd:4, u8 mask, u8 value`; в бинарнике intf/base/cmd упакованы иначе,
чем символические `RTW_PWR_INTF_*`/`RTW_PWR_ADDR_*` в mainline, поэтому
сравнивались **количество, порядок и оффсеты** — они не зависят от упаковки).
Разбор `.data+0x33d38`:

| Поток | 8822E, шт. | 8822C, шт. | Оффсеты |
|---|---|---|---|
| carddis→cardemu | 8 | 8 | `0086 0086 002E 002D 007F 004A 0005 FFFF` — **совпадают** |
| cardemu→act | 22 | 22 | `0000 0005 0075 0006 0075 FF1A 002E 0006 0005 1018 0005 0005 0074 0071 0062 0061 001F 00EF 1045 0010 1064 FFFF` — **совпадают** |
| act→cardemu | 12 | 12 | `0093 001F 00EF 1045 FF1A 0049 0006 0002 0005 0005 0000 FFFF` — **совпадают** |
| cardemu→carddis | **11** | 12 | у 8822E нет первого шага `{0x0005, SDIO, WRITE, BIT(7), BIT(7)}` (`rtw8822c.c:4795`-блок), остальные совпадают |

Отличие в disable объяснимо (гипотеза, не проверено): SDIO card-disable делает
HALMAC через CMD52 до таблицы; на поведение probe это не влияет.

### 4.2 BB-таблица — ~97 % совпадения

Сравнение множеств пар `(addr, value)` (addr < 0x100000):

| Таблица | 8822E пар | 8822C пар | совпало | только 8822E |
|---|---|---|---|---|
| `array_mp_8822e_phy_reg` ↔ `rtw8822c_bb` | 1808 | 2051 | **1739 (96.7 %)** | 64 |

Отличающиеся регистры сосредоточены в аналоговой части: `0x0808, 0x0818, 0x081C,
0x08A0, 0x09B4, 0x0A74-0x0A80`, `0x180C-0x1820` / `0x410C-0x4120` (per-path bias),
`0x1830` / `0x4130` (кодовая таблица усиления, у 8822E свои `0x700F…0x70FB`),
`0x1908, 0x3804/0x3830/0x3904/0x3930 (=0x45800000)`.
То есть **RF/аналог другой, цифровой BB почти тот же**.

### 4.3 AGC/RF/cal-init/txpwr — другие

| Таблица | 8822E (слов u32) | 8822C (слов) | Комментарий |
|---|---|---|---|
| AGC | 7 978 | 3 728 | у 8822E расширенный набор (3840 записей в `0x1D90`), значения иные (`0x300001BE` vs `0x300001FF`) |
| RF A | 5 606 | 40 070 | у 8822C таблица разбита на большее число cut/RFE-веток — сравнение «в лоб» некорректно |
| RF B | 6 362 | 40 706 | то же |
| cal-init | 5 222 | 4 920 | другой набор калибровочных записей |
| txpwr_lmt | 2 976 | — | у 8822C 3 таблицы (type0/1/5), у 8822E две |

Плюс отдельные `_8822e`-реализации всех RF-калибровок (в `rtw8822c.c`
аналогичных функций 44, уникальных имён 63): `_iqk_{2g,5g}_{txk,rxk}_iqk_8822e`,
`_dpk_*` (30+ функций), `halrf_tssi_dck_8822e` (4668 Б), `_halrf_tssi_8822e`,
`_halrf_txgapk_*`, `halrf_rxspurk_8822e`, `_halrf_*thermal*`.
Итого 199 016 Б chip-specific кода.

### 4.4 SDIO

`HALMAC_PG_NUM_SDIO_8822E` (6 записей × 16 Б) для SDIO: `hq=nq=lq=exq = 64`,
`gapq = 1` — совпадает с `page_table_8822c[0] = {64,64,64,64,1}`
(`rtw8822c.c:4920-4926`), а индекс `[0]` в mainline — это именно SDIO
(`mac.c:1265-1290`, `case RTW_HCI_TYPE_SDIO`). `HALMAC_RQPN_SDIO_8822E` — те же 6 наборов по 7 u32
(значения 0..3 = маппинги DMA), структурно = `rtw_rqpn` в mainline
(`mac.c:1095-1108`, для SDIO тоже индекс `[0]`).
Размер блока/транспорт: mainline жёстко ставит 512 (`sdio.c:1527`), вендор тоже
512 (строка `SDIO ver=%u clock=%uMHz blk_size=%u bytes, io_indir=%u`).
Отдельные 8822E-функции (`init_sdio_cfg_8822e`, `get_sdio_clk_cnt_8822e`,
`get_sdio_int_lat_8822e`, `get_sdio_tx_addr_8822e`, `tx_allowed_sdio_8822e`,
`mac_pwr_switch_sdio_8822e`, `phy_cfg_sdio_8822e`, `intf_tun_sdio_8822e`,
`update_sdio_free_page_8822e`, `set_sdio_clk_mon_8822e`) в mainline аналогов
per-chip не имеют — всё в общем `sdio.c`. Значения таймингов/int-latency
**не извлечены** (см. §8).

### 4.5 Efuse — не подтверждено

Известно только: `rtl8822e_read_efuse` (2936 Б), `dump_efuse_map_8822e`,
`rtw_halmac_read_{physical,logical}_efuse_map`, `EFUSE_GetArrayLen_MP_8822E_MSDIO`
над 128-байтным `Array_MP_8822E_MSDIO` (это MP-карта, не рабочая раскладка).
Структура `struct rtw8822c_efuse` (`rtw8822c.h:61-105`: `channel_plan` @0xb8,
`xtal_k` @0xb9, `rfe_option` @0xc7, `country_code` @0xc8, thermal @0xd0…) для
8822E **не сверена**. Размеры 512/768/124 взяты из 8822C и не проверены.

---

## 5. Оценка вариантов

### (a) Добавить `0xa822` к `rtw8822cs` (+ свой `hw_spec` с fw 8822E)

Объём: ~40 строк.
1. `rtw8822e.c` — копия `rtw8822c_hw_spec` (`.id = RTW_CHIP_TYPE_8822C` можно
   оставить, чтобы не трогать общий код) с `.fw_name = "rtw88/rtw8822e_fw.bin"`.
2. ID в таблице: `SDIO_DEVICE(0x024c, 0xa822)` → `&rtw8822e_hw_spec`.
3. `include/linux/mmc/sdio_ids.h`: `#define SDIO_DEVICE_ID_REALTEK_RTW8822ES 0xa822`.
4. Kconfig/Makefile: `RTW88_8822ES` (select 8822C + SDIO).
5. `MODULE_FIRMWARE("rtw88/rtw8822e_fw.bin")` + положить извлечённый blob.

Почему имеет шанс: power-seq идентичен, paging/rqpn совпадают, формат прошивки
совместим, BB почти тот же, загрузчик не проверяет подпись/тип чипа, cut-версия
читается из `REG_SYS_CFG1`, а не из таблицы.
Риск: RF-калибровки (IQK/DPK/TSSI) на 8822C-таблицах дадут мусорные коэффициенты
→ «карта видна, но не ассоциируется» либо падение txpower; efuse-раскладка может
дать неверный MAC/RFE → неправильный天线/мощность.
Цена ошибки: 1–2 дня, включая прошивку и DTS. **Делать первым.**

### (b) Форк backend `rtw8822e`

Объём (по фактическим размерам):
- `rtw8822e.c` ≈ копия `rtw8822c.c` (5451 стр.) — из них ~44 RF-функции
  (IQK/DPK/TSSI/TXGAPK/spur/thermal) переписать/адаптировать;
- `rtw8822e_table.c` — конвертация 9 таблиц: BB 10.4 КБ, BB-PG 1.1 КБ,
  AGC 31.9 КБ, RF A 22.4 КБ, RF B 25.4 КБ, cal-init 20.9 КБ, txpwr 11.9+6.9 КБ
  (~130 КБ данных; конвертер пишется один раз по образцу существующего);
- `rtw8822e.h` (efuse-структура после RE), `rtw8822es.c` (ID-таблица);
- 41 колбэк `rtw_chip_ops` — большинство копируется, но
  `phy_calibration`, `dpk_track`, `pwr_track`, `set_tx_power_index`,
  `read_efuse`, `cfg_ldo25`, `power_on/off` требуют сверки с вендором.
Реалистично: 2–4 недели на человека, знающего rtw88, + железо под рукой.
Пока (a) не провалилась — не начинать.

### (c) Оставить вендорский модуль через compat-слой — невозможно

| Барьер | Факт |
|---|---|
| Vermagic | `4.9.y SMP preempt mod_unload modversions aarch64` — ядро 7.3 откажется грузить (`vermagic` + отсутствие `modversions`) |
| ABI | 214 неразрешённых символов, среди них внутренние: `__cfg80211_alloc_event_skb`, `cfg80211_del_sta_sinfo`, `cfg80211_inform_bss_frame_data`, `_rtw_sdio_multi_state`, плюс `__kmalloc/__netdev_alloc_skb` с CRC 4.9 |
| Структуры | `cfg80211_ops`, `net_device`, `sdio_driver`, `ieee80211_*` за 4.9→7.3 менялись многократно — вызовы уйдут по неверным смещениям (падение, не «ошибка загрузки») |
| Архитектура | модуль — **полный MAC** со своим 802.11-стеком (`rtw_cfg80211_ops`), дублирует mac80211; в 7.3 его пришлось бы адаптировать целиком, а не «подключить» |
| Ядро | сборка под 4.9 `-mcmodel=large`, `-fno-pic`, 8051/3081-специфичные тайминги; cfg80211 API (scan/P2P/HE) несовместим |

Вывод: «взять вендорский .ko» — не вариант ни в каком виде; единственный
разумный путь — использовать его как **источник таблиц и прошивки**.

---

## 6. План по этапам

| Этап | Работа | Критерий приёмки |
|---|---|---|
| **W0** (сделано) | Досье, REA-артефакт, извлечение прошивки | `mainline/docs/wifi.md`, `docs/evidence/wifi-artifact.json`, `rtw8822e_fw.bin` (199 568 Б, sha256 `d82bf903…`) |
| **W1** | DTS: `&sd_emmc_a` (bus-width 4, non-removable, keep-power-in-suspend, `max-frequency` 100 МГц, `cap-sd-highspeed`), `regulator-fixed` на GPIOX_8, pwrseq, (опц.) PWM-32k | `cat /sys/kernel/debug/mmc*/ios` показывает 4-bit/50 МГц; в `dmesg` нет `mmc1: error -110` |
| **W2** | Эксперимент (a): SDIO ID + `rtw8822e_hw_spec` + fw-файл | `ls /sys/bus/sdio/devices` → `mmc2:0001:1` (vendor `0x024c`, device `0xa822`); dmesg: `fw version …`, без `failed to download firmware`/`invalid fw key` |
| **W3** | Диагностика PHY: если fw загрузилась, смотреть калибровки | dmesg без `IQK`/`TSSI`/`pwr_track` ошибок; `iw dev wlan0 info` без `-EBUSY` |
| **W4** | Сверка efuse: снять карту с вендорской прошивки (`/proc/net/rtl88x2es/*efuse*`) и сравнить с `rtw8822c_efuse` | MAC/`rfe_option`/`channel_plan`/`xtal_k` совпадают; иначе — правка структуры |
| **W5** | Если (a) не ассоциируется — форк (b): таблицы 8822E + RF-калибровки | `iw wlan0 scan`, `wpa_supplicant` подключение, `iperf3` ≥ 100 Мбит/с на 5 ГГц |
| **W6** | BT-коэкзистенция (`btrtl`+`hci_h5` 3-wire, тот же чип) | одновременно Wi-Fi + BT-мышь/наушники без деградации |

---

## 7. Проверка на железе

PCI-пути нет (`lspci` не нужен), смотрим SDIO/MMC:

```sh
# 1. Устройство на шине
ls -l /sys/bus/sdio/devices/            # ожидается mmc1:0001:1 (или mmc2:…)
cat /sys/bus/sdio/devices/*/vendor /sys/bus/sdio/devices/*/device   # 0x024c / 0xa822
cat /sys/bus/sdio/devices/*/modalias    # sdio:c07v024CdA822

# 2. Хост-контроллер
cat /sys/kernel/debug/mmc1/ios          # bus width 4, clock 50000000/100000000
dmesg | grep -iE 'mmc|sdio|rtw88'

# 3. Драйвер
dmesg | grep -iE 'rtw|firmware|IQK|TSSI|pwr_track|efuse'
lsmod | grep rtw88

# 4. Сеть
ip link show wlan0
rfkill list
iw dev wlan0 info
iw dev wlan0 scan | head -40
iw reg get
```

Эталон для сравнения — вендорская прошивка: `dmesg` при `insmod 88x2es.ko`
содержит `CHIP TYPE: RTL8822E`, `SDIO ver=… clock=…MHz blk_size=512`, а карта
efuse доступна через `rtw_proc_create_dir`/`proc_get_efuse_map`
(`dump_efuse_map_8822e`) — снять до перехода на mainline.

---

## 8. Открытые вопросы (что нужно проверить)

1. **Efuse-раскладка 8822E.** Размеры 512/768/124 и смещения (`0xb8` channel_plan,
   `0xc7` rfe_option и т.д.) — из 8822C, для 8822E не подтверждены. Как: снять
   `/proc/net/rtl88x2es/efuse_map` на вендорском ядре + Ghidra на
   `rtl8822e_read_efuse` (`0x109a80`, 2936 Б) и `_halrf_get_efuse_tssi_offset_8822e`.
2. **32k LPO.** Нужен ли внешний 32.7 кГц (PWM-канал 0 из `wifi_pwm_conf`) для
   probe/сна, или 8822E обходится внутренним, как 8822C в mainline. Как: probe без
   PWM-клока + замер `iw dev wlan0 get power_save`/LPS.
3. **SDIO-тайминги 8822E**: `get_sdio_int_lat_8822e`, `set_sdio_clk_mon_8822e`,
   `cfg_sdio_tx_page_threshold_88xx`, `cfg_txagg_sdio_align_88xx` — извлечь
   константы (дизасм/`.rodata`), сравнить с `rtw88/sdio.c:951,1527`.
4. **TX power limit type 5** — у 8822E две таблицы (`txpwr_lmt`, `…_type5`), у
   8822C три (`rtw8822c_txpwr_lmt_type0/1/5`); понять, какая применяется для RFE
   этой платы (`rfe_option` из efuse).
5. **AGC**: почему у 8822E 3840 записей `0x1D90` против 1863 пар у 8822C —
   другой формат индексации или расширенная таблица.
6. **Плата**: `rfe_type`/антенны (2T2R?), нужен ли `rtw8822c_rfe_defs`-аналог.

---

## 9. Доказательная база

**Артефакт (REA):** `mainline/docs/evidence/wifi-artifact.json`
(`ev_e3d0155accdc…`, sha256 `88x2es.ko` = `f81edba0…`).

**Символы-доказательства в `88x2es.ko`** (смотреть `nm -S`, `aarch64-linux-gnu-objdump`):

```
*firmware*   array_mp_8822e_fw_nic, array_length_mp_8822e_fw_nic, rtl8822e_fw_dl,
             rtw_halmac_dlfw{,_mem,_from_file}, dlfw_to_mem_88xx, start_dlfw_88xx
*halmac*     halmac_init_adapter, halmac_deinit_adapter, rtw_halmac_cfg_phy_para,
             rtw_halmac_c2h_handle, rtw_halmac_download_rsvd_page,
             _halmac_sdio_cmd52_read/_write, _halmac_sdio_reg_read_8/16/32/_n
*sdio*       init_sdio_cfg_8822e, get_sdio_clk_cnt_8822e, get_sdio_int_lat_8822e,
             get_sdio_tx_addr_8822e, tx_allowed_sdio_8822e,
             mac_pwr_switch_sdio_8822e, phy_cfg_sdio_8822e, intf_tun_sdio_8822e,
             update_sdio_free_page_8822e, set_sdio_clk_mon_8822e,
             cfg_tx_fmt_sdio_8822e, HALMAC_{PG_NUM,RQPN}_SDIO_8822E
*mac init*   rtl8822e_init_hal_spec, rtl8822e_hal_init, rtl8822e_mac_verify,
             init_{system,trx,wmac,edca,protocol,low_pwr,h2c,sdio}_cfg_8822e
*phy/rf*     odm_config_bb_{phy,agc,phy_reg_pg,txpwr_lmt}_8822e,
             odm_read_and_config_mp_8822e_{phy_reg,agc_tab,radioa,radiob,cal_init},
             _iqk_*_8822e, _dpk_*_8822e, _halrf_tssi_*_8822e, _halrf_txgapk_*_8822e,
             halrf_rxspurk_8822e, _halrf_*thermal*_8822e, config_phydm_*_8822e
*efuse*      rtl8822e_read_efuse, dump_efuse_map_8822e, pg_efuse_by_map_8822e,
             rtw_halmac_read_{physical,logical}_efuse_map, Array_MP_8822E_MSDIO
*power*      platform_wifi_power_on/off (no-op), rtl8822e_power_on,
             card_en_flow_8822e / card_dis_flow_8822e,
             TRANS_{CARDDIS_TO_CARDEMU,CARDEMU_TO_ACT,ACT_TO_CARDEMU,CARDEMU_TO_CARDDIS}_8822E
```

**Вендорские исходники (пути из строк модуля):**
`…/quasar/hardware/wifi/realtek/drivers/8822es/rtl88x2ES/hal/rtl8822e/`
(`rtl8822e_ops.c`, `rtl8822e_phy.c`, `sdio/rtl8822es_halinit.c`),
`hal/phydm/phydm_rtl8822e.c`, `hal/hal8822e_fw.c`.

**Mainline (Linux 7.3, `65ba8f34ec88`, 2026-10-27):**
`rtw88/rtw8822cs.c:12-17`, `rtw88/Kconfig:108-117`, `rtw88/rtw8822c.c:5339,5342,
5348-5350,5373-5378,4858-4868,4920-4926`, `rtw88/mac.c:417-438,697-740`,
`rtw88/sdio.c:951,1527,1535`, `rtw88/sdio.h:146`, `rtw88/fw.h:12-13,316-343`,
`rtw88/main.h:191-201`, `rtw88/main.c:1890`,
`include/linux/mmc/sdio_ids.h:129-138`, `rtw88/Makefile`, `rtw89/Makefile`.
