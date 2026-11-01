# `ai/drivers/wireless/wifi/` — Wi-Fi подсистема Яндекс.Станции

Здесь собрано всё, что относится к Wi-Fi: userspace-стек `wifid`, обёртка над
`wpa_supplicant`, watchdog-ресетер, переключатель прошивок и точка доступа.

**49 файлов, 300 КБ.** Источники: `smart_devices/platforms/yandexstation_2/` и
`yandex_io/` (полный Quasar SDK). Отдельно: `realtek/` — манифесты Realtek-модулей
(исходников в архивах нет, см. [`realtek/README.md`](realtek/README.md)).

---

## Краткий ответ: что за Wi-Fi модуль в Станции 2

**Intel Dual Band Wireless-AC 7265D** (семейство 7265), подключён по **PCIe**,
адрес `0000:01:00.0`.

| Параметр | Значение |
|---|---|
| Драйвер | `iwlwifi` + `iwlmvm` + `mac80211` + `cfg80211` |
| Прошивка №1 (штатная) | `iwlwifi-7265D-22.ucode`, свойство `from.4.9` |
| Прошивка №2 (backport) | `iwlwifi-7265D-29.ucode`, свойство `from.backport` |
| Штатные модули | `/vendor/lib/modules/` |
| Backport-модули | `/vendor/lib/backported_iwlwifi/` (+ `compat.ko`) |
| Версия прошивки (sysfs) | `/sys/bus/pci/drivers/iwlwifi/0000:01:00.0/firmware_version` |
| Версия драйвера (sysfs) | `/sys/bus/pci/drivers/iwlwifi/0000:01:00.0/driver_version` |
| Свойство Android | `persist.vendor.iwlwifi.version` |
| Интерфейс | `wlan0` |
| Температура | `/sys/class/hwmon/<hwmon>/temp1_input` → метрика `wifiTemperature` |

То есть **не Broadcom и не Realtek**. Broadcom/Realtek/MediaTek/Qualcomm в
манифестах — это либо заготовки общих Amlogic BSP, либо модули **других**
устройств (см. раздел 7).

---

## 1. `firmware-switcher/` — переключение прошивки 22 ↔ 29

Источник: `platforms/yandexstation_2/platformd/firmware_switcher/`

`firmware_switcher.{h,cpp}` — низкоуровневая часть:

```cpp
enum class FwVer { UNSUPPORTED, DEFAULT_22, BACKPORT_29 };

FwVer getCurrentFirmware();          // читает persist.vendor.iwlwifi.version
std::string getLoadedFirmware();     // firmware_version из sysfs
std::string getLoadedDriver();       // driver_version из sysfs
bool changeToDefault(WifiSwitcher);  // backport -> 22
bool changeToBackport(WifiSwitcher); // 22 -> backport
```

Порядок переключения (`changeFirwmare`):

1. Проверить, что текущая версия — ожидаемая (`from.4.9` или `from.backport`).
2. Записать новое значение в `persist.vendor.iwlwifi.version`.
3. Сравнить **загруженную** прошивку из sysfs с целевой.
4. Если отличается:
   - `svc wifi disable`
   - `rmmod` в правильном порядке:
     - старый драйвер: `iwlmvm, iwlwifi, mac80211, cfg80211`
     - backport: `compat, iwlmvm, iwlwifi, mac80211, cfg80211`
   - `stop wificond`
   - `stop android.hardware.wifi@1.0-service`
   - `insmod` из `/vendor/lib/modules/` или `/vendor/lib/backported_iwlwifi/`
   - `start android.hardware.wifi@1.0-service`
   - `start wificond`
   - пауза 2 секунды
   - `svc wifi enable` (через `getSystemCommandOutputTail`, т.к. `system()` не работает)
5. Вернуть `true`, если версия изменилась.

Верхнеуровневая логика — в `station2/core/platformd.cpp`
(`class WifiFirmwareSwitcher`):

- подписка на системный конфиг **`wifi_firmware`**;
- поля конфига: `version` (`22`/`29`), `reloadDriver` (bool), `forceReboot` (bool);
- на boot запоминается `onBootVersion_`;
- при смене версии отправляется телеметрия `wifiFirmware` с `{fw, driver}`;
- если `currentVersion != onBootVersion && forceReboot` → `system("reboot")`.

Пример конфига с бэкенда:

```json
{ "version": 29, "reloadDriver": true, "forceReboot": false }
```

---

## 2. `wifid/` — основной Wi-Fi демон (20 файлов)

Источник: `yandex_io/services/wifid/` + `yandex_io/scaffolding/wifid/`

`wifid` — отдельный процесс Quasar, IPC-сервис с именем **`wifid`**.
Запускается из `maind --wifid`, поднимает `WpaClientSupplicant` и `WifiManager`.

### Файлы

| Файл | Строк | Что |
|---|---:|---|
| `wifi_endpoint.cpp/.h` | 919 + 149 | IPC-сервер: список сетей, подключение, статус, интернет-чек |
| `wifi_manager.cpp/.h` | 934 + 108 | Обёртка над `wpa_cli`: scan, add/enable/remove network, статус |
| `wifi_utils.cpp/.h` | 206 + 38 | WEP/WPA/OPEN, конвертация enum ↔ строки wpa_supplicant |
| `wifi_types.h` | 153 | `ScanResult`, `WifiConfiguration`, `DetailedState` |
| `http_head.cpp/.h` | 164 | HEAD-запрос для `generate_204` |
| `internet_checker.cpp/.h` | 64 | Два варианта проверки интернета (curl / generic) |
| `scaffolding/wifid.cpp` | 92 | Точка входа сервиса и регистрация в `maind` |
| `tests/`, `testlib/` | — | Тесты endpoint и фейковый wpa-клиент |

### IPC-команды

`wifi_list_request`, `wifi_connect`, `async_wifi_connect`, `wifi_disable`,
`wifi_enable`, `wifi_turn_off`, `wifi_turn_on`, `network_config_reload`.

При подключении клиента сервер сразу отдаёт `wifi_status` и `wifi_list`.

### Как происходит подключение

1. `shrinkConfiguredNetworks()` — если сохранено ≥ `maxStoredWifiNetworks`
   (по умолчанию **10**), удаляются самые старые.
2. Тип сети: если клиент явно передал `wifi_type` — используется он, иначе
   угадывается по `capabilities` из scan results (`[WEP` → WEP, `[WPA` → WPA,
   иначе OPEN).
3. `WpaUtils::startWifiConnect()`:
   - SSID оборачивается в кавычки;
   - OPEN → `KeyMgmt::NONE`;
   - WEP → `wepKeys[0]`, `WEP40`, `NONE`;
   - WPA → `preSharedKey`, `WPA_PSK`, протоколы `RSN+WPA`, pairwise `CCMP+TKIP`,
     group `WEP40/WEP104/CCMP/TKIP`;
   - `addNetwork()`, затем `enableNetwork(netId, true)` и `reassociate()`.
4. Ожидание состояния `CONNECTED` / `CONNECTED_NO_INTERNET`
   (`connectivityStateChangeTimeoutSeconds`, по умолчанию **40 с**).
5. При ошибке авторизации → `AUTH_ERROR`, при таймауте → `TIMEOUT`
   (сеть удаляется).
6. `saveConfiguration()`.
7. `generate_204` — проверка интернета.
8. Ожидание `waitForInternetAfterConnectSec` (по умолчанию **15 с**).
9. **NTP-синхронизация**: команда из `common.ntpClientCommand`, сервер —
   случайный из `common.ntpServerList` (по умолчанию `ru.pool.ntp.org`).

### Проверка интернета

`generate_204`: HEAD на `http://<host>/generate_204`, успех — **ровно код 204**
(редиректы captive portal игнорируются). 3 попытки. Период:
10 с обычный, 2 с ускоренный. Таймаут запроса 2 с; при таймауте сбрасывается
DNS-кеш.

### Список сетей

`buildWifiList()`:

- дубликаты по SSID отбрасываются;
- `wifi_type` = WEP/WPA/OPEN;
- `is_corporate = true`, если в capabilities есть `EAP`;
- `secure = (WEP || WPA)`;
- `rssi`, `mac` (BSSID), `freq` (из поля `rate` вывода `scan_results`),
  `channel` (формула Android: 2484 → 14, <2484 → `(freq-2407)/5`,
  иначе `freq/5-1000`);
- `is_connected` — сравнение BSSID с текущим.

Уровень сигнала: 4 градации, `MIN_RSSI=-100`, `MAX_RSSI=-50`
(копия Java-функции Android). Enum: `WEAK, GOOD, STRONG, EXCELLENT`.

### Статусы

`NOT_CONNECTED`, `CONNECTING`, `CONNECTED_NO_INTERNET`, `CONNECTED`,
`NOT_CHOSEN`; плюс `signal`, `internet_reachable`, `current_network`.

### Конфиг `wifid` (Станция 2)

```json
"wifid": {
    "accessPointStartTimeoutSec": 0,
    "configuringRescanTimeoutMs": 30000,
    "Logging": { "level": "info" }
}
```

Значения по умолчанию в коде:

| Ключ | Default | Смысл |
|---|---:|---|
| `rescanTimeoutSec` | 3600 | Обычный период сканирования |
| `configuringRescanTimeoutSec` | 10 | Период в режиме настройки |
| `connectivityStateChangeTimeoutSeconds` | 40 | Ожидание CONNECTED |
| `waitForInternetAfterConnectSec` | 15 | Ожидание интернета после коннекта |
| `maxStoredWifiNetworks` | 10 | Лимит сохранённых сетей |
| `reassociateAfterEnable` | true | Делать `reassociate` после `enableNetwork` |
| `wpaCtrlSocketDir` | `/var/run/wpa_supplicant` | Каталог control-сокета |

### Скрытые сети

`isHiddenNetwork()` считает сеть скрытой, если клиент **явно** указал
`wifi_type` (значит, SSID введён вручную и его нет в scan results).

---

## 3. `wpa-cli/` — обёртка над `wpa_supplicant` (4 файла)

Источник: `yandex_io/libs/wpa_cli/`

`WpaClient` — абстрактный интерфейс; `WpaClientSupplicant` — реализация через
**пропатченный hostap**:

```cpp
#include <contrib/restricted/patched/hostap_client/wpa_supplicant/wpa_cli.h>
```

Это не запуск `wpa_cli` как процесса, а **линковка с `libwpa_client`**:
`wpa_ctrl_*`, `eloop_*`, `wpa_cli_cmd_*`.

- `startThread()` запускает `eloop_run()` в отдельном потоке;
- `wpa_cli_set_monitor_callback()` — события supplicant'а
  (`CTRL-EVENT-*`) приходят в колбэк;
- все команды защищены мьютексом (`WpaGuard`), потому что `wpa_cli` использует
  один статический буфер и на команду, и на ответ;
- поддержаны: `scan`, `list_networks`, `add_network`, `select_network`,
  `enable_network`, `enable_all_networks`, `disable_network`,
  `disable_all_networks`, `remove_network`, `reload_config`, `save_config`,
  `status`, `signal_poll`, `scan_results`, `set_network`, `get_network`,
  `disconnect`, `reconnect`, `reassociate`.

---

## 4. `wifi-reseter/` — watchdog Wi-Fi (7 файлов)

Источник: `yandex_io/libs/wifi_reseter/`

`WifiReseter` — «умный» ресетер: следит за счётчиками интерфейса и доступностью
интернета, а не просто перезагружает Wi-Fi по таймеру.

### Триггеры

| Триггер | Что проверяет |
|---|---|
| `minTx` / `minRx` | слишком мало пакетов за интервал (`TooLowTrigger`) |
| `maxTxErrors` / `maxRxErrors` | слишком много ошибок (`TooHighTrigger`) |
| `noInternet` | интернет недоступен дольше `intervalSec` |
| `autoReset` | прошло `intervalSec` с предыдущего сброса |
| `dailyReset` | ежедневно в `HH:MM` (`parseTime`) |
| `sixClicksEnabled` | **6 нажатий кнопки** → принудительный hard-reset |

Плюс `minResetIntervalSec` — минимальный интервал между сбросами
(по умолчанию **15 минут**), чтобы не устраивать цикл перезагрузок.

### Два метода сброса

**`OLD`** — мягкий: отправить `network_config_reload` в `networkd`.

**`HARD`** — жёсткий:

1. `turnWifiOff()`:
   - Station: сообщение `wifi_turn_off` в `wifid`;
   - Mini: `wifi_networks_disable` + ожидание ответа + `ip link set wlan0 down`.
2. Дождаться, пока `wlan0` опустится (netlink-монитор).
3. Если `needPciRemove()` (только Station):
   - записать `1` в `/sys/bus/pci/devices/0000:00:00.0/remove`;
   - дождаться исчезновения интерфейса;
   - обнулить счётчики;
   - записать `1` в `/sys/bus/pci/rescan`;
   - дождаться появления `wlan0`.
4. `turnWifiOn()`; для Station — `svc wifi enable` + `wifi_turn_on`.
5. Дождаться `UP` и появления IP-адреса.

> **Интересная деталь:** удаляется PCI-устройство `0000:00:00.0`, хотя
> iwlwifi сидит на `0000:01:00.0`. Похоже на ошибку/особенность платформы
> (root port), но именно так в коде.

### Телеметрия

Событие `wifiReset` с полями `offMs`, `onMs`, `addrMs`, `total`, `method`
(`hard`/`old`), `reason` (`rx`, `tx`, `rxErrors`, `txErrors`, `nointernet`,
`autoreset`, `daily`, `button`). Отдельные события:
`wifiResetCountersRestored`, `wifiResetNoCountersTooLong`.

### Конфиг

Подписка на системный конфиг **`wifi_reseter2`**:

```json
{
  "minResetIntervalSec": 900,
  "sixClicksEnabled": true,
  "minTx":  { "intervalSec": 60, "limit": 10, "method": "hard" },
  "minRx":  { "intervalSec": 60, "limit": 10, "method": "hard" },
  "maxTxErrors": { "intervalSec": 60, "limit": 100, "method": "old" },
  "maxRxErrors": { "intervalSec": 60, "limit": 100, "method": "old" },
  "noInternet": { "intervalSec": 300, "method": "hard" },
  "autoReset":  { "intervalSec": 86400, "method": "hard" },
  "dailyReset": { "time": "04:00", "method": "hard" }
}
```

Счётчики берутся из netlink (`rx_packets`, `tx_packets`, `rx_errors`,
`tx_errors`). Если счётчиков нет дольше **3 минут** — телеметрия
`wifiResetNoCountersTooLong`, но сброс не делается.

Кнопка: `resetWifiByButton()` вызывается из `buttons_handler` Станции 2
(6 нажатий), но только если `sixClicksEnabled`.

---

## 5. `firstrund-access-point/` — точка доступа при настройке

Источник: `yandex_io/services/firstrund/access_point.{h,cpp}`

`ScriptsAccessPoint` дергает shell-скрипты:

- `/system/bin/startap.sh "<accessPointName>"`
- `/system/bin/stopap.sh`

Имя точки доступа для Станции Макс: **`Yandex-Station-Max`**
(`common.cfg` → `firstrund.accessPointName`).
В `setupd` есть скрытая команда, поднимающая AP с серийником:
`startap.sh Yandex-Station-Max-<serial>`.

При настройке: `wifid` → `wifi_disable` → `startap.sh`; после успешного
подключения — `stopap.sh`. Скрипты в архивах **отсутствуют** (лежат в
buildroot/Android-образе).

---

## 6. `wifi-info/` — детальная телеметрия Wi-Fi (5 файлов)

Источник: `yandex_io/libs/net/wifi_info.{h,cpp}` (1 136 строк),
`sdk/wifi_info.h`, `sdk/converters/wifi_info_converter.*`

Собирает информацию напрямую через **netlink / nl80211** (не через wpa_cli):

- `NL80211_CMD_GET_INTERFACE`, `GET_STATION`, `GET_SCAN`, `GET_SURVEY`;
- BSS: SSID, BSSID, frequency, signal, IE;
- IE: country, `associated_stations`, `channel_utilization`,
  `average_access_delay_usec`, rates;
- rate info: `mbits`, `MCS`, `VHT_MCS/NSS`, `HE_MCS/NSS`, `short_guard`,
  `he_guard_interval`, `HE_DCM`;
- flags: ESS/IBSS/PRIVACY/QOS/APSD/RADIO_MEASURE/DEL_BACK/IMM_BACK и т.д.;
- station flags: AUTHORIZED, WME, MFP, TDLS_PEER, ASSOC…;
- TID-статистика (`tids`);
- survey по каналам: шум, время занятости, `channel_utilization`.

Настройки `WifiInfoSettings`: `allBss`, `iface`, `tids`, `bothIE`, `longCpblt`,
`survey`. Используется для диагностики/телеметрии (`getDetailedWifiInfo`).

---

## 7. Какие Wi-Fi модули вообще упоминаются в манифестах

### 7.1. Станция 2 (ветка `yandexstation_2/88`)

| Репозиторий | Путь | Назначение |
|---|---|---|
| `yandex-io/kernel/drivers/backport-iwlwifi` | `vendor/yandex/drivers/backport-iwlwifi` | **backport-драйвер iwlwifi 4.9 → 5.x** |
| `yandex-io/platform/hardware/intel/libwifihal-intel` | `hardware/intel/libwifihal-intel` | HAL для Intel Wi-Fi |
| `unity/platform/frameworks/opt/net/wifi` | `frameworks/opt/net/wifi` | Android Wi-Fi framework |
| `unity/platform/system/connectivity/wificond` | `system/connectivity/wificond` | `wificond` |
| `unity/platform/system/connectivity/wifilogd` | `system/connectivity/wifilogd` | `wifilogd` |
| `unity/platform/hardware/amlogic/wifi` | `hardware/amlogic/wifi` | Amlogic-обвязка |
| `unity/platform/hardware/broadcom/libbt` | `hardware/broadcom/libbt` | **Bluetooth** (не Wi-Fi) |
| `unity/p-amlogic/vendor/broadcom/btusb` | `vendor/amlogic/common/broadcom/btusb` | **Bluetooth USB** |

То есть для Wi-Fi Станции 2 в дереве живут **Intel iwlwifi + backport**, а
Broadcom здесь только для Bluetooth.

### 7.2. Buildroot-манифест (`buildroot_firmware/master`) — это Station Plus

В той же папке лежит отдельный buildroot-манифест (ветка
`yandexstation_plus`, репозиторий `yandex-io/buildroot-manifest`). В нём
перечислены **реальные Wi-Fi-модули других устройств**:

| Репозиторий | Путь | Модуль |
|---|---|---|
| `unity/platform/hardware/wifi/broadcom/drivers/ap6xxx` | `vendor/broadcom/src/ap6xxx` | **Broadcom AP6xxx** (AP6255/AP6256/AP6356 — 802.11ac + BT) |
| `unity/platform/hardware/wifi/realtek/drivers/8822cs` | `vendor/realtek/src/8822cs` | **Realtek RTL8822CS** (802.11ac 2×2 + BT 5.0, SDIO) |
| `yandex-io/realtek/bt_uart_8822cs` | `vendor/realtek/src/8822cs_bt_uart` | **Bluetooth-часть RTL8822CS** по UART |
| `yandex-io/buildroot-external-broadcom` | `vendor/broadcom` | Buildroot-пакет Broadcom |
| `yandex-io/buildroot-external-realtek` | `vendor/realtek` | Buildroot-пакет Realtek |

Плюс в этом же манифесте — йандексовые драйверы ядра, которых нет в
`smart_devices`:

`quasar-led-ring`, `quasar-led-screen`, `quasar-pcba-ids`, `rotary-volume`,
`tas5825m` (усилитель), `tlv320adc3101` (ADC), `es7210` (ADC), `es8156` (DAC),
`aw9110` (LED-драйвер), `cy8cmbr31xx` (касание), `ltr308als01` (свет),
`pcm1774`, `sy6045s` (питание), `led_panel_midi`, `vl53l0x_ctrl` (ToF).

### 7.3. Наследие Amlogic BSP в манифестах `yandexstation_2/*`

Стандартный «зоопарк» Amlogic, который тянется во все версии прошивки
(это **не** значит, что всё используется):

| Вендор | Драйверы |
|---|---|
| Broadcom | `ap6xxx` |
| Realtek | `8188eu`, `8188ftv`, `8189es`, `8189ftv`, `8192cu`, `8192du`, `8192es`, `8192eu`, `8723au`, `8723bs`, `8723bu`, `8723ds`, `8723du`, `8811au`, `8812au`, `8822bs`, `8822bu` |
| MediaTek | `mt7601`, `mt7668` |
| Qualcomm | `qca6174`, `qca9377` |
| Icomm | `ssv6xxx` |

Это USB/SDIO-драйверы из `hardware/wifi/*/drivers/*` — их наличие в manifest
означает лишь то, что дерево Amlogic их содержит. Для Станции 2 активен
только Intel 7265D.

**Подробно по Realtek** (все 17 модулей, чипы, шины, revision, фрагменты
манифестов, команды `repo sync`) — в [`realtek/README.md`](realtek/README.md).
Там же зафиксировано, что **исходников Realtek в архивах нет**: только записи
`manifest.xml`, без единого `.c`/`.h`/`.ko`.

---

## 8. Диагностика и метрики

| Что | Откуда |
|---|---|
| `wifiFirmware` (event) | `platformd`: `{ "fw": "...22.ucode", "driver": "..." }` |
| `wifiTemperature` (gauge) | `/sys/class/hwmon/<wifi>/temp1_input` |
| `wifiReset` (key-values) | `wifi_reseter`: `offMs/onMs/addrMs/total/method/reason` |
| `hwAddrInfo` | `ifconfig -a` + `/sys/class/net/*` → MAC всех интерфейсов |
| `WifiNetwork.kt` | Android SDK: телеметрия сети (SSID/RSSI) |
| `wifi_info` | nl80211: BSS, survey, TID, IE, rates |

---

## 9. Чего здесь нет

| Отсутствует | Где искать |
|---|---|
| Исходники `iwlwifi`/`iwlmvm`/`mac80211` | `yandex-io/kernel/drivers/backport-iwlwifi` (только ссылка в manifest) |
| Бинарники прошивок `iwlwifi-7265D-22/29.ucode` | `linux-firmware` / вендорский образ |
| `.ko`-модули (`iwlwifi.ko`, `compat.ko` …) | `/vendor/lib/modules`, `/vendor/lib/backported_iwlwifi` на устройстве |
| `startap.sh` / `stopap.sh` / `wireless_iface_up.sh` | Android/buildroot-образ (`/system/bin`, `/system/vendor/quasar`) |
| Исходники `wpa_supplicant` | `contrib/restricted/patched/hostap_client` (внутренний патченный hostap) |
| Драйверы Broadcom/Realtek | репозитории в манифестах Station Plus; исходников в архивах нет — см. [`realtek/README.md`](realtek/README.md) |

---

## 10. Итого: 5 самых интересных фактов про Wi-Fi Станции 2

1. **Железо — Intel 7265D**, а не Broadcom/Realtek; в манифестах Station 2
   есть `backport-iwlwifi` и `libwifihal-intel`.
2. **Две прошивки на выбор** — 22 (штатная, `from.4.9`) и 29 (backport,
   `from.backport`), переключаются на лету через `rmmod/insmod` и
   `persist.vendor.iwlwifi.version`, при необходимости — с `reboot`.
3. **Ресетер Wi-Fi умеет hard-reset через PCI**: снимает устройство
   `0000:00:00.0` и делает `rescan` — причём это root port, а iwlwifi на
   `0000:01:00.0`.
4. **6 нажатий кнопки** — скрытый hard-reset Wi-Fi (если включён
   `sixClicksEnabled`), плюс ежедневный сброс в заданное время.
5. **`wifid` — не просто обёртка**: сам считает RSSI/каналы, проверяет
   интернет через `generate_204` (ровно 204), лимитирует 10 сохранённых
   сетей и после подключения сам запускает NTP-синхронизацию.
