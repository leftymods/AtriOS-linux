# Porting plan

Targets: **Linux 7.3**, **U-Boot v2026.10**. Order agreed with the maintainer:
everything else first, Wi-Fi last.

## 1. LED ring (first)

| Item | Vendor | Mainline |
|---|---|---|
| LED controller | `leds-is31fl32xx.ko` (upstream + 3 vendor tweaks) | `drivers/leds/leds-is31fl32xx.c` + patch |
| Framebuffer/trigger | `quasar_led_ring.ko` (`led_ring_fb.c`) | new `drivers/leds/trigger/ledtrig-ringfb.c` |
| Userspace | `/usr/lib/atrios/light` writes `/dev/fb5` | unchanged |

Needed changes:

1. `leds-is31fl32xx`: compatible `issi,is31fl3236,gva`, `reg_offset` fallback,
   `max-brightness-scale` → `led_classdev.max_brightness`.
2. `ledtrig-ringfb`: 24x1 RGB888 fbdev, 20 ms refresh, trigger `led_ring_fb`,
   channels sorted by `station2:<colour>:ring<N>`.
3. DTS: two `is31fl3236` nodes on the mainline I2C controller, 36 children
   each, plus `led-ring-fb` platform node.

Validation on target:

```sh
ls /sys/class/leds/station2:*:ring*           # 72 devices, max_brightness 200
cat /sys/class/leds/station2:red:ring1/trigger # [led_ring_fb]
python3 -c 'open("/dev/fb5","wb").write(bytes([255,0,0])*24)'  # ring red
```

## 2. Front matrix 25x16

`gowin_led_device` (fbdev + backlight) and `quasar_led_screen` (SPI protocol,
JTAG reconfiguration of the Gowin FPGA). No upstream equivalent: new driver,
needs the SPI framing and the reconfiguration sequence reversed from the
binary. Userspace `/usr/lib/atrios/screend` writes `/dev/fb4` (25x16 RGB888).

## 3. Rotary, keys, IR

- `rotary-volume`: small input driver (2 GPIOs → `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`).
  Port as `drivers/input/misc/rotary-volume.c`; upstream `rotary-encoder` emits
  relative events and would need a userspace daemon.
- `gpio-keys` (mic mute, function) → mainline `gpio-keys`.
- IR → `meson-ir` with the vendor keymap.

## 4. Ambient light

`ltr308als01` is register-compatible with the upstream `ltrf216a` driver
(address 0x53, same register map). Patch: add `lite-on,ltr308als01` to the
of_match table. Userspace reads the IIO channel.

## 5. PCBA ids

`quasar_pcba_ids` parses two EEPROMs and exposes attributes. Mainline:
`at24`/`nvmem` + a small userspace helper (board revision is only used to pick
animation variants).

## 6. Zigbee

`zigbee-control` is a UART protocol (`/dev/ttyAML4`) with reset/boot GPIOs.
Port as a serdev driver; protocol details have to be reversed from the module
(commands are visible as strings).

## 7. Audio

Vendor ASoC card uses TDM-B with es7210 (ADC), es8156 (headphones) and two
sy6045s (speakers). Mainline has es7210 and es8156 drivers; sy6045s is not
upstream. Needs: meson TDM DAI on SM1, codec DT nodes, and a new sy6045s
driver (or reuse tas5825m-style amp driver).

## 8. Bluetooth

`bt-dev` + vendor H5 loader. Mainline: `btrtl` + `hci_h5` with
`CONFIG_BT_HCIUART_3WIRE`, DT node for the UART and the reset GPIO. No vendor
code needed, only the right firmware and serdev description.

## 9. Wi-Fi (last)

RTL8822ES, SDIO `024c:a822`, vendor `88x2es.ko` v5.15.0.1. Mainline 7.3 has
no 8822E backend (rtw88 covers 8822B/C/CS only). Work: new `rtw8822e` chip
backend or extend the 8822C one, plus firmware partition; this needs the
vendor driver decompiled (REA + Ghidra) to recover the PHY tables and the
SDIO/efuse differences.

## U-Boot

Mainline U-Boot already supports SM1 (odroid-c4, vim3l). Work: board defconfig
(SD/eMMC boot, 2 GiB LPDDR4), DTS with the eMMC/SD/UART/ethernet nodes, and a
boot script that keeps the Android partition layout used by the installer.
