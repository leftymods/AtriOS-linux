# Realtek Wi-Fi (`rtw` / RTL8xxx) — что есть в архивах

## Короткий ответ

**Исходников драйверов Realtek в архивах нет.** Есть только **ссылки в manifest.xml**
(имя репозитория, путь в дереве, revision) — 17 модулей для Station 2 / Amlogic BSP
и 3 записи для Station Plus (RTL8822CS). Ни одного `.c`, `.h`, `Makefile` или `.ko`
Realtek в распакованных архивах не найдено.

Поэтому в этой папке лежит всё, что реально можно было извлечь: **фрагменты
манифестов + индекс + таблица модулей**. Чтобы получить сам код, нужен доступ к
внутреннему Gerrit и `repo sync` (см. «Как достать исходники»).

> Не путать: это **вендорские драйверы Realtek** (`rtl8xxxu`-подобные, из
> `hardware/wifi/realtek/drivers/*`), а не upstream-драйверы ядра `rtw88`/`rtw89`.
> В архивах нет ни тех, ни других.

---

## Состав папки

| Файл | Что внутри |
|---|---|
| `manifests/station2-22.xml.fragment` | 17 записей Realtek из `firmware/yandexstation_2/22/manifest.xml` |
| `manifests/station2-88.xml.fragment` | те же 17 записей из `firmware/yandexstation_2/88/manifest.xml` |
| `manifests/station-plus-buildroot-master.xml.fragment` | `8822cs`, `buildroot-external-realtek`, `bt_uart_8822cs` |
| `realtek-manifest-index.txt` | все 89 манифестов с Realtek, сгруппированные по совпадению блока |
| `all-references.txt` | краткое описание групп манифестов |

---

## 17 модулей Realtek в манифестах Station 2 / Amlogic BSP

Репозитории: `unity/platform/hardware/wifi/realtek/drivers/<module>`
Путь в дереве: `hardware/wifi/realtek/drivers/<module>`
Ветка: `yandexstation_2/22` … `yandexstation_2/88` (одинаковые revision во всех версиях)

| Модуль | Чип | Шина | Антенны / диапазон | Revision |
|---|---|---|---|---|
| `8188eu` | RTL8188EU | USB 2.0 | 1×1, 2.4 ГГц, 802.11n | `8ab9d717fae6f2c0e86a29b3ed13605e416b0949` |
| `8188ftv` | RTL8188FTV | USB 2.0 | 1×1, 2.4 ГГц, 802.11n | `70cd24d27517bd896e0267c46ca2ea92e146421a` |
| `8189es` | RTL8189ES | SDIO | 1×1, 2.4 ГГц, 802.11n | `5a59235d2b69e774a58e0139dfd558170b866f9a` |
| `8189ftv` | RTL8189FTV | SDIO | 1×1, 2.4 ГГц, 802.11n | `11cdc07dd29ab4055ec76974e246708ae8457605` |
| `8192cu` | RTL8192CU | USB 2.0 | 2×2, 2.4 ГГц, 802.11n | `3ed1865d6a2816d039e0ad5f8337fd481b0b1f9d` |
| `8192du` | RTL8192DU | USB 2.0 | 2×2, 2.4/5 ГГц, 802.11n | `6462ef1ef11da009d626329ff050f630612f7459` |
| `8192es` | RTL8192ES | SDIO | 2×2, 2.4 ГГц, 802.11n | `0ca8ed4ff7ffb02e3fd44423c1f3ff119535627f` |
| `8192eu` | RTL8192EU | USB 2.0 | 2×2, 2.4 ГГц, 802.11n | `f50b97e4f3e8f8f136c9e7c3472c83d96a100333` |
| `8723au` | RTL8723AU | USB 2.0 | 1×1, 2.4 ГГц + BT 4.0 | `7e3ec155dfa552d57418bc1dadbb1d9ac80afc1d` |
| `8723bs` | RTL8723BS | SDIO | 1×1, 2.4 ГГц + BT 4.0 | `143ba772cbe9119220e29ccfac540010197eb56e` |
| `8723bu` | RTL8723BU | USB 2.0 | 1×1, 2.4 ГГц + BT 4.0 | `5cd485220f64358bcb8a8c26c26eb9640645fcc6` |
| `8723ds` | RTL8723DS | SDIO | 1×1, 2.4 ГГц + BT 4.2 | `6dd89cf13632c0229303baa6eaa16e916b66d8d4` |
| `8723du` | RTL8723DU | USB 2.0 | 1×1, 2.4 ГГц + BT 4.2 | `c4b669f889cb6ebc2934017c5adb9f9361a605af` |
| `8811au` | RTL8811AU | USB 2.0 | 1×1, 2.4/5 ГГц, 802.11ac | `8efa520f9ecb9fd29757dcdc289775a2dd856820` |
| `8812au` | RTL8812AU | USB 3.0 | 2×2, 2.4/5 ГГц, 802.11ac | `6b50ecd24ff4bcd8175e0bf7db485629d3088a8e` |
| `8822bs` | RTL8822BS | SDIO | 2×2, 2.4/5 ГГц, 802.11ac + BT 4.2 | `d2a5452a08084b3838083ba1eda3d1aeab82ee7c` |
| `8822bu` | RTL8822BU | USB 3.0 | 2×2, 2.4/5 ГГц, 802.11ac + BT 4.2 | `b1032e12315f76cb7b80f0005750672100963148` |

Все 17 записей присутствуют **во всех** версиях прошивки Station 2 (от `-4` до `/88`),
независимо от реального железа. Это стандартный набор Amlogic BSP.

## Station Plus (buildroot) — RTL8822CS

| Репозиторий | Путь | Revision | Что это |
|---|---|---|---|
| `unity/platform/hardware/wifi/realtek/drivers/8822cs` | `vendor/realtek/src/8822cs` | `af1ee06eb77ba9cd3f87fb07b839185319ee15bf` | RTL8822CS: 802.11ac 2×2 + BT 5.0, SDIO |
| `yandex-io/realtek/bt_uart_8822cs` | `vendor/realtek/src/8822cs_bt_uart` | `d7bd6851fda2a499630f31324204101bdacbfa45` | Bluetooth-часть 8822CS через UART |
| `yandex-io/buildroot-external-realtek` | `vendor/realtek` | `ddec9e5dc05e0882e036c0332c69436211b480b3` | Buildroot-пакет Realtek |

---

## Что это значит для Станции 2

**Станция 2 (Макс) использует Intel Dual Band Wireless-AC 7265D** (PCIe,
`0000:01:00.0`, `iwlwifi` + `iwlmvm`). Realtek-модули в её манифесте — это
наследство общего Amlogic-дерева; они не собираются и не загружаются на этом
устройстве.

Realtek реально нужен:
- **Station Plus** — RTL8822CS (SDIO);
- другим устройствам на Amlogic BSP — 8188/8192/8723/8812 и т.п.

---

## Как достать исходники

В манифестах указан внутренний Gerrit (`ssh://git@bb.yandex-team.ru/quas/`,
`gerrit.yandex-team.ru`). Команды (при наличии доступа):

```bash
repo init -u ssh://git@bb.yandex-team.ru/quas/manifest -b yandexstation_2/22
repo sync unity/platform/hardware/wifi/realtek/drivers/8188eu
# или весь набор:
repo sync $(repo manifest | grep -o 'name="[^"]*wifi/realtek[^"]*"' | cut -d'"' -f2)
```

Для Station Plus:

```bash
repo init -u ssh://git@bb.yandex-team.ru/quas/manifest -b yandexstation_plus
repo sync unity/platform/hardware/wifi/realtek/drivers/8822cs \
           yandex-io/realtek/bt_uart_8822cs \
           yandex-io/buildroot-external-realtek
```

Альтернатива без доступа к Яндексу — публичные драйверы Realtek:
`rtl8188eu`, `rtl8192eu`, `rtl8723ds`, `rtl8822bs`/`rtl8822bu` (GitHub, форки
`lwfinger`, `morrownr`) или upstream `rtw88`/`rtw89` в ядре Linux (но это уже
другие драйверы, не вендорские).

---

## Чего в архивах нет

| Отсутствует | Где искать |
|---|---|
| Исходники 17 модулей + 8822CS | внутренний Gerrit, `unity/platform/hardware/wifi/realtek/drivers/*` |
| `.ko`-модули (`8188eu.ko`, `8822cs.ko`, …) | образ устройства, `/vendor/lib/modules/` |
| Прошивки firmware (`rtl8188eufw.bin`, `rtl8822cs_fw.bin`) | `linux-firmware`, `vendor/realtek/` |
| `wpa_supplicant`-конфиги Realtek | образ устройства, `/vendor/etc/wifi/` |
| DTS с описанием SDIO/USB Realtek | отдельный репозиторий ядра Amlogic |
