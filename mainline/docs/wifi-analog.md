# RTL8822ES: публичный аналог вендорского драйвера

Проверено 2026-10-29. Только факты с URL/`file:line`; предположения помечены.

## Вердикт

Полный исходник вендорского SDIO-драйвера опубликован:
**Cohen0415/rtl8822cs-driver**, дерево `rtl8822cs/`, дроп
`RTL8822ES_RTL8822CS-VE_WiFi_linux_v6.1-20260513`, DRIVERVERSION
`v5.15.0.1-36-g081fbcc78.20221018_COEX20220722-0000`, GPL-2.0.

Он содержит всё, что мы добывали реверсом: таблицы BB/AGC/RF/cal-init/txpwr,
efuse-маски SDIO, прошивку `array_mp_8822e_fw_nic`, HALMAC 8822E и
`platform/platform_aml_s905_sdio.c`.

`OpenIPC/devourer` — **не** MAC-драйвер и **не** SDIO: это userspace-libusb
со своим стеком. Ценен как эталон PHY/RF-калибровок 8822E и список HW-квирков
(`docs/8822e-quirks.md`).

Стратегия: **(A) rtw88 + `0xa822` + `hw_spec`** первым, но таблицы берём из
исходника, а не из бинарника. (B) вендорский дроп на 7.3 — реалистичный
fallback (это исходник, не 4.9-бинарник). (C) форк `rtw8822c → rtw8822e`
подешевел до ~1–2 недель, потому что таблицы теперь в текстовом виде.

## Источники

| Источник | Лицензия | Что поддерживает | Статус |
|---|---|---|---|
| github.com/Cohen0415/rtl8822cs-driver | GPL-2.0 | **8822ES SDIO** (`sdio_intf.c` → `024c:a822`, `RTL8822E`) + 8822EU USB + 8822EE PCIe | v5.15.0.1-36 (2022-10-18), дроп 2026-05 |
| khadas/…_realtek_8822es (vim4-android14) | GPL-2.0 | то же, layout `rtl88x2ES/` (как в путях нашего `.ko`) | v5.15.0.1-5 |
| LineageOS/…_realtek-drivers_8822es (lineage-24.0) | GPL-2.0 | то же | v5.15.0.1-5, 2026-08 |
| github.com/OpenIPC/devourer | GPL-2.0 | 8822E **только USB** (`0bda:a81a`/`e822`), userspace, SDIO нет | активен |
| github.com/sbwml/package_kernel_rtl8822cs | GPLv2 | 8822CS only (нет `hal/rtl8822e/`) | OpenWrt main |
| github.com/libc0607/rtl8822cs-20240221 | GPL-2.0 | 8822CS only | 2026-05 |
| github.com/tuo3657/rtw88-wifi-fix-s905l3b | — | rtw88 на S905L3B: `rfe 255` fallback, 100 МГц → 400 кГц | 2026-08 |
| github.com/lwfinger/rtw88 | GPL-2.0 | бэкпорт wireless-next, 8822E нет | 2026-10 |
| lore.kernel.org | — | Wi-Fi-патчей 8822E/8822ES **нет**; только `btrtl` v2 с `/* 8822E */` | 2026-10 |
| gitee | — | `rtl8822es`/`8822es` → пусто | 2026-10 |

## Совпадение с нашим RE

| Таблица | Наш `88x2es.ko` | Исходник |
|---|---|---|
| `array_mp_8822e_phy_reg` | 10 400 Б | 2600 u32 = 10 400 Б (`halhwimg8822e_bb.c`) |
| `array_mp_8822e_agc_tab` | 7 978 слов | 7 978 |
| `array_mp_8822e_radioa/radiob` | 22 424 / 25 448 Б | есть (`halhwimg8822e_rf.c`) |
| `array_mp_8822e_cal_init` | 20 888 Б | есть (`halrf_rfk_init_8822e.h`) |
| `txpwr_lmt[_type5]` | 11 904 / 6 944 Б | есть |
| `Array_MP_8822E_MSDIO[_BT]` | 128 / 64 Б | есть |
| `array_mp_8822e_fw_nic` | 199 568 Б, sha256 `d82bf903…` | ~199.6 КБ, дроп -36 (наш -17) |

`platform_aml_s905_sdio.c`: `platform_wifi_power_on/off` — пустышка при
`LINUX_VERSION_CODE >= 3.14`; питанием рулит Amlogic `aml_wifi`, значит в
mainline это только DTS.

## Новые факты, которых не было в бинаре

1. `rfe_option` у 8822E — логический **`0xca`** (devourer), а
   `struct rtw8822c_efuse` кладёт его на `0xc7` → при stage (a) RFE-таблицы
   могут примениться не те.
2. DPK на rfe 21/22 принудительно обходится и ядром.
3. IQK чистит RF-бит `TX_CCK_IND` 40 МГц — нужен фикс порядка калибровок.
4. IGI floor `0x1e`, а не `0x1c` (на `0x1c` MCS4+ RX не декодируется).
5. 1SS OFDM: `0x820 = 0x32` @5G / `0x31` @2.4G, `0x1e2c = 0x0400`; иначе MCS0 wedge.
6. RF reg `0x0` пишется только через FON-порт `0x1808`/`0x4108`.
7. SDIO TX-формат: `rtl8822es_sdio_tx_format_update()` (`HALMAC_SDIO_AGG_MODE`
   vs `DUMMY_AUTO_MODE`), `hal/rtl8822e/sdio/rtl8822es_halinit.c`.
8. Вендорский SDIO-драйвер не читает `SYS_CFG2`: тип чипа — по SDIO device id
   (`sdio_intf.c`); chip-id 8822E (`0x17` USB vs `0x12` из дизасма) на SDIO
   надо замерить.

## Пути

| | A: rtw88 + `0xa822` | B: дроп out-of-tree на 7.3 | C: форк rtw8822c→e |
|---|---|---|---|
| Лицензия | GPL-2.0 | GPL-2.0 (не upstreamable) | GPL-2.0 |
| SDIO | есть | полный вендорский | есть |
| Таблицы 8822E | из исходника | внутри | из исходника |
| Совместимость 7.3 | родная | неизвестна (guard'ы до 6.1) | родная |
| Объём | ~40 строк | порт KE + форк | ~1–2 недели |
| Риск | efuse/RFE/DPDT | API-дрейф | время |

## Проверить на железе

efuse-дамп (`rfe_option` 0xca vs 0xc7); SYS_CFG2 chip-id; per-chain RSSI A/B
(DPDT/eFEM); `0x820`/`0x1e2c`; при ошибках init — 100 МГц → 50/25 МГц;
2.4 ГГц TX (у devourer он не декодировался и под вендорским ядром).
