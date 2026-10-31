# AtriOS Bluetooth Initial Setup Specification (BLE Provisioning)

## 1. Overview
This document specifies the architecture and protocol for future out-of-the-box headless setup of Yandex Station 2 over Bluetooth Low Energy (BLE).

Until enabled in production, Bluetooth operates strictly in **silent mode** (`hciconfig hci0 noscan`, no BLE advertising, no inquiry/page scanning) to maintain security and zero radio pollution.

---

## 2. Triggering Setup Mode

1. **First Boot (Headless)**:
   - If `/etc/wpa_supplicant/wpa_supplicant.conf` has no active networks or `wlan0` has no IP address within 30 seconds of boot:
     - The daemon enters **Provisioning Mode** for 10 minutes.
     - LED ring displays slow blue pulsing animation.
2. **Manual Trigger (Factory Reset / Network Reconfiguration)**:
   - User holds **Top Button #1** (KEY_POWER / KEY_VOICECOMMAND) for **5 seconds**.
   - LED ring flashes amber 3 times, then enters slow blue pulsing animation.
   - Provisioning service starts advertising.
3. **Automatic Termination**:
   - Once Wi-Fi is connected and an IPv4 address is obtained, BLE advertising stops immediately and the radio returns to silent mode (`noscan`).
   - If no connection is made within 10 minutes, advertising times out and the radio returns to silent mode.

---

## 3. BLE Advertising Packet

- **Flags**: `0x06` (LE General Discoverable Mode, BR/EDR Not Supported).
- **Complete Local Name**: `YandexStation-XXXX` (where `XXXX` is the last 4 characters of the Wi-Fi MAC address).
- **Service UUID**: 128-bit Custom Setup Service UUID:
  - `A7B10001-5741-4E44-4558-53544154494F` (`ATRIOS-SETUP-SVC`)

---

## 4. GATT Architecture

### Service: AtriOS Provisioning (`A7B10001-5741-4E44-4558-53544154494F`)

| Characteristic | UUID | Properties | Description |
|---|---|---|---|
| **Device Info** | `A7B10002-...` | Read | Device model, firmware version, serial number |
| **Wi-Fi Scan List** | `A7B10003-...` | Read, Notify | JSON list of scanned SSIDs, RSSI, and security flags |
| **Wi-Fi Credentials** | `A7B10004-...` | Write | Encrypted JSON `{ "ssid": "...", "psk": "..." }` |
| **Setup Status** | `A7B10005-...` | Read, Notify | Current connection status & assigned IP |

---

## 5. Provisioning State Machine

```
   [ IDLE / SILENT ]
          │ (Button hold 5s OR First boot without Wi-Fi)
          ▼
   [ BLE ADVERTISING ] (Blue pulsing LED ring)
          │
          │ Client Connects & Reads Wi-Fi Scan List
          │ Client Writes Credentials to Char 0x0004
          ▼
   [ CONNECTING TO WI-FI ] (White rotating LED ring)
          ├── Auth Failed / Wrong Key ──► [ ERROR: AUTH_FAILED ] ──► (Retry)
          ├── DHCP Timeout            ──► [ ERROR: NO_IP ]       ──► (Retry)
          └── Success (IP Assigned)   ──► [ SUCCESS ] (Green pulse 2s)
                                                │
                                                ▼
                                         [ RETURN TO SILENT ]
                                         (Disable BLE advertising)
```

---

## 6. Security & Encryption
- Transport uses standard BLE LE Secure Connections (LE SC) with numeric comparison or ECDH (Curve25519) key exchange before exchanging network credentials.
- Wi-Fi credentials written to Characteristic `0x0004` are wiped from memory immediately after passing to `wpa_supplicant`.

---

## 7. Implementation Blueprint
- **Backend Daemon**: `/usr/lib/atrios/ble-setupd` (Python 3 using `bluez` D-Bus GATT server API or lightweight raw HCI socket implementation via `rtk-bt-init`).
- **Wi-Fi Management**: Integrates directly with `/usr/lib/atrios/wireless wifi connect "<SSID>" "<PSK>"`.
