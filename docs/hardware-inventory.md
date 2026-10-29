# Hardware inventory (from vendor DTB + binaries)

Sources of truth:

- vendor DTB: `fw-pdm3/sm1.dtb` (sha256 `f70a9b41…`), decompiled to DTS
- vendor DTS: `AtriOS-linux/tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts`
- vendor modules: `board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/*.ko`
- vendor userspace: `board/yandex/station2/rootfs_overlay/usr/lib/atrios/{light,screend,input}`

## SoC / board

- Amlogic S905X3 (SM1, G12A family), 4x Cortex-A53, Mali-G31
- 2 GiB LPDDR4, 14.6 GiB eMMC (AJTD4R), RTL8822ES Wi-Fi/BT combo (the SD slot is
  not usable: its CMD/CD pins are shared with the ring I2C bus)
- PMIC-less design: always-on rails, `20V_AMPL` fixed regulator on a GPIO
- Boot: eMMC, Android partition table, `system` = ext4 rootfs

## Displays / LEDs (I2C + SPI)

`i2c@1f000` (vendor bus, `clock-frequency = 400000`):

| Device | Address | Compatible |
|---|---|---|
| EEPROM | 0x56 | `24c01` |
| Ambient light | 0x53 | `liteon,ltr308` (upstream `ltrf216a` driver) |
| LED ring left (LED 1-12) | 0x3c | `issi,is31fl3236,gva` |
| LED ring right (LED 13-24) | 0x3f | `issi,is31fl3236,gva` |

`spi@15000` (vendor `num-cs = 1`, `cs-gpios`):

| Device | Compatible | Notes |
|---|---|---|
| Front matrix 25x16 | `ya,led_screen` (binds `gowin_led_device`), `atri,led-panel` | Gowin FPGA, JTAG reconfig, 400-byte frames |

GPIOs used by the panel (vendor numbering, decoded in `docs/screen.md`):
reset, JTAG TCK/TDI/TDO/TMS, JTAG reconfig, JTAG select, plus an SPI CS that is
shared with the JTAG TMS line.

## Input

| Device | Compatible | Data |
|---|---|---|
| Rotary volume | `rotary-volume` | 2 GPIOs, emits `KEY_PAGEUP`/`KEY_PAGEDOWN` (104/109) |
| Buttons / mic mute | `amlogic,gpio_keypad` | `yandex,gpio-keypad`, GPIOA_15/GPIOA_14, codes 582/248 |
| IR receiver | `amlogic,meson-ir` | NEC, vendor keymap |

## Wireless

| Device | Bus | Vendor driver | Mainline |
|---|---|---|---|
| Wi-Fi RTL8822ES | SDIO `024c:a822` | `88x2es.ko` (v5.15.0.1) | **none** (rtw88 has 8822B/C/CS only) |
| Bluetooth | UART `ttyS1`, H5 3-wire, GPIOX_17 reset | `bt-dev` + vendor H5 | `btrtl` + `hci_h5` (needs 3WIRE) |

## Audio

Vendor ASoC card (`auge_sound`, TDM-B) with:

- `es7210` 4-channel ADC (mic array), I2C 0x40
- `es8156` headphone DAC, I2C 0x08
- `sy6045s` two speaker amps, I2C 0x2b (woofer) and 0x2a (tweeters)
- `pcm1774`, `tas5825m`, `tlv320adc3101` modules shipped but not wired in this DT

## Other

- `quasar_pcba_ids`: parses PCBA EEPROMs, exports `/sys/.../pcba_ids`
- `zigbee-control`: UART (`/dev/ttyAML4`), reset/boot GPIOs
- `quasar_led_ring`: framebuffer + LED trigger feeding the ring
- `gowin_led_device`: framebuffer `/dev/fb4`, backlight, 25x16 RGB888
- eMMC/SD/Ethernet/USB/HDMI are standard Amlogic SM1 blocks with mainline drivers
