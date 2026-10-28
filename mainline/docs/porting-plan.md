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

1. `leds-is31fl32xx`: the DTS uses the standard `issi,is31fl3236a`
   compatible; the patch adds `max-brightness-scale` (200) and also accepts
   the vendor `issi,is31fl3236,gva` + `reg_offset` for vendor DTBs.
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

- `rotary-volume`: small input driver (2 GPIOs). Vendor DT uses
  `key-codes = <104 109>` = `KEY_PAGEUP`/`KEY_PAGEDOWN`, and
  `/usr/lib/atrios/rotaryd` listens exactly for those (confirmed from the
  module and userspace), so the DTS and driver must keep 104/109, not
  `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`. Port as
  `drivers/input/misc/rotary-volume.c`; upstream `rotary-encoder` emits
  relative events and would need a userspace daemon.
- buttons: the vendor keypad is scanned (period + detect_mode), so the port
  keeps a dedicated `drivers/input/misc/atrios-keypad.c` with
  `compatible = "yandex,gpio-keypad"`; plain `gpio-keys` cannot express it.
  Codes are `KEY_VOICECOMMAND` (582, function) and `KEY_MICMUTE` (248).
- IR → `meson-ir` with the vendor keymap.

## 4. Ambient light

`ltr308als01` is register-compatible with the upstream `ltrf216a` driver
(address 0x53, same register map) and upstream already exposes the
`liteon,ltr308` compatible, so the DTS uses that and no driver patch is
needed. Userspace reads the IIO channel.

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
sy6045s (speakers). **Correction: none of the three codecs is in mainline
7.3** — there is no `es7210.c`, `es8156.c` or `tas5825m.c` in
`sound/soc/codecs` (only es7134/es7241/es831x and tas5805m). All three drivers
have to be ported from the vendor sources.

The SM1 audio side *is* upstream and complete via AXG/G12A drivers:
`amlogic,sm1-tdmin`, `amlogic,sm1-tdmout`, `amlogic,sm1-pdm`; TDM-B pads are
on the AO bank (`tdm_ao_b_*` = GPIOAO_6/7/8/4/9) and PDM on
GPIOC_0..4, matching the vendor groups exactly.

Consequences and findings (see `docs/audio.md` and
`docs/audio-dts-fragment.dts`):

- mainline uses the DPCM card model (`axg-card.c`), so the front-end PCM
  numbering changes: 0 = playback (FRDDR_A), 1 = capture TDM-B (es7210),
  2 = capture PDM. `/etc/asound.conf` and the `for dev in 0 3 1` loop in
  `usr/lib/atrios/audio` must be updated for the mainline image.
- the `S03amplifiers` `/dev/mem` poke of `0xff660020/0x24` is a vendor
  workaround and is not needed: in mainline those are clock muxes and the
  routing is done by `tdmif_b`.
- `sound-name-prefix` must stay Woofer/Tweeters/Headphone/Loopback so the
  existing mixer control names survive; `SPK mute` and `Audio Power Save`
  cannot be reproduced (scripts already tolerate that).
- sy6045s is fully reversible from the vendor module (DWARF is present):
  regmap 8/32-bit, register-width table in `.rodata`, firmware text format,
  MD5+crc32, restore-reg triples, TLV volume (-79.5 dB, 0.5 dB step).

## 8. Bluetooth

`bt-dev` + vendor H5 loader. Mainline: `btrtl` + `hci_h5` over serdev.
Required: `BT`, `BT_BREDR`, `BT_LE`, `BT_HCIUART`, `BT_HCIUART_SERDEV`,
`BT_HCIUART_3WIRE`, `BT_HCIUART_RTL` (selects `BT_RTL`), `SERIAL_DEV_BUS`,
`SERIAL_DEV_CTRL_TTYPORT`, `SERIAL_MESON`, `RFKILL`, `CRC_CCITT`.
DT: `&uart_A` with `uart-has-rtscts`, child
`compatible = "realtek,rtl8822cs-bt"`, `enable-gpios = <&gpio GPIOX_17
GPIO_ACTIVE_HIGH>` (binding allows only enable/device-wake/host-wake/max-speed).

Findings (see `docs/bluetooth.md`):

- vendor `rtl8822cs_*` and `rtl8822es_*` firmware/config files are byte
  identical; the firmware is a valid V2 `RTBTCore` blob (one section, len
  44013) but its trailing header carries **project id 33**, which is not in
  `project_id_to_lmp_subver[]` in 7.3, so `btrtl` rejects it;
- the 8822E has no dedicated support: `btrtl` has one special case for
  `hci_rev 0x000e` that re-detects the chip and then gives up with
  `unknown IC info`; whether this board reports `0x000e` or `0x000c` must be
  measured from dmesg;
- plan A: official linux-firmware `rtl_bt/rtl8822cs_*`; plan B: add
  `{ RTL_ROM_LMP_8822B, 33 }` to the project-id table plus a `rom_version`
  guard. The vendor H5 loader stays only for the 4.9 tree.

## 9. Wi-Fi (last)

RTL8822ES, SDIO `024c:a822`, vendor `88x2es.ko` v5.15.0.1. Mainline 7.3 has
no 8822E backend (rtw88 covers 8822B/C/CS only). Reverse engineering (see
`docs/wifi.md`) shows it is 8822C-class silicon with a different SDIO ID and
different RF/analogue tables:

- power-on/off sequences are identical step by step; SDIO paging equals
  `page_table_8822c[0]`;
- digital BB init is 96.7 % identical (1739/1808 pairs); differences sit in
  the analogue blocks (`0x0808-0x0A80`, `0x180C-0x1820`/`0x410C-0x4120`);
- AGC/RF/cal-init/txpower are different (own 8822E IQK/DPK/TSSI code);
- the NIC firmware is embedded in the vendor `.ko` (199 568 B) and its header
  already matches `rtw_fw_hdr`, so it can be extracted into
  `rtw88/rtw8822e_fw.bin`;
- efuse layout is **not** confirmed (taken from 8822C) — the main unknown.

Two-stage plan:

1. stage (a): add `0xa822` with a `rtw8822e_hw_spec` cloned from 8822C and a
   different `fw_name` (~40 lines) — quick bring-up, failure mode is "card
   probes but does not associate";
2. stage (b): fork the 8822C backend into `rtw8822e` (5451 lines, 41 chip ops,
   ~130 KB of generated tables, 44 RF calibrations) if stage (a) is not
   enough.

The vendor module cannot be loaded on 7.3 (vermagic 4.9 + modversions, 214
unresolved symbols). DTS needs `&sd_emmc_a`, a fixed regulator/pwrseq and the
32 kHz question resolved. Note: `lib/firmware/rtlbt/rtl8822es_fw` is the
*Bluetooth* firmware, byte-identical to `rtl8822cs_fw`, not Wi-Fi.

## U-Boot

Mainline U-Boot already supports SM1 (odroid-c4, vim3l), but there is no
`CONFIG_TARGET_*` for meson: the board is selected through
`CONFIG_SYS_VENDOR`/`CONFIG_SYS_BOARD`, so the port adds
`board/amlogic/yandex-station-max/` (MAC from efuse, `fdtfile`).

Facts confirmed against v2026.10:

- SM1 uses the `CONFIG_MESON_G12A` profile; DT sources now live under
  `dts/upstream/src/arm64/amlogic/` with `OF_UPSTREAM=y`;
- `DISTRO_DEFAULTS` is deprecated in favour of `BOOTSTD_DEFAULTS`
  (`bootflow scan`);
- `u-boot.bin.sd.bin` is not produced by U-Boot: it is the 512-byte headed
  image made by `aml_encrypt_g12a --bootmk` / `amlogic-boot-fip`;
- **the BL2/DDR firmware is the main risk**: the vendor BL2 is built for this
  board's 2 GiB LPDDR4 (1392 MHz, `board_id` selection), while odroid-c4 FIP
  targets 4 GiB DDR4. The safe path is to replace only BL33 in the existing
  vendor FIP and re-sign it with gxlimg; verify the FIP offset (0 vs 512) by
  dumping the first eMMC sectors before flashing.

Board files and the U-Boot-only DTS are in `mainline/u-boot/`; the build
sequence and the on-hardware checklist are in `docs/uboot.md`.
