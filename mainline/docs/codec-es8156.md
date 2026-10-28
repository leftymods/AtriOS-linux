# ES8156 headphone DAC (Linux 7.3)

The ES8156 is the 3.5 mm jack DAC of the Yandex Station Max.  It sits on
vendor `i2c@1d000` (mainline `i2c2`) at address `0x08`, is codec-2 of the
TDM-B link in the mainline DTS fragment, and carries
`sound-name-prefix = "Headphone"`.

There is no upstream driver (`sound/soc/codecs/` has no `es8156.c` and
`Kconfig` has no `SND_SOC_ES8156` in 7.3).  This document records the port
in `drivers/audio/es8156.c` / `patches/linux/0008-soc-es8156.patch`.

Repository layout: `mainline/drivers/audio/es8156.c` is the review copy and
source of truth; patch 0008 installs the identical file as
`sound/soc/codecs/es8156.c` and wires it into the codec Kconfig/Makefile.
`scripts/apply.sh` also copies everything under `mainline/drivers/` into
`<tree>/drivers/`, so it will additionally place an unused copy at
`drivers/audio/es8156.c` and print a "not referenced from Makefile"
warning.  That copy is harmless (it is not built); the patch-installed
`sound/soc/codecs/es8156.c` is the one that matters.

## Evidence

Everything below was read out of the vendor module

```
board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/snd-soc-es8156.ko
```

which is not stripped and carries full DWARF (`.debug_info`, `.debug_line`,
`.debug_str`).  Source path recorded in DWARF:

```
/place/sandbox-data/tasks/.../quasar/out/target/product/yandexstation_2/
obj/vendor_modules/es8156/es8156.c
```

Commands used:

```sh
pahole -C es8156_priv snd-soc-es8156.ko
pahole -C snd_soc_dai_driver snd-soc-es8156.ko
nm -n -S snd-soc-es8156.ko
aarch64-linux-gnu-objdump -dr --disassemble=es8156_i2c_probe snd-soc-es8156.ko
aarch64-linux-gnu-objdump -dr --disassemble=es8156_probe snd-soc-es8156.ko
aarch64-linux-gnu-objdump -dr --disassemble=es8156_set_bias_level snd-soc-es8156.ko
aarch64-linux-gnu-objdump -dr --disassemble=es8156_set_dai_fmt snd-soc-es8156.ko
aarch64-linux-gnu-objdump -dr --disassemble=es8156_pcm_hw_params snd-soc-es8156.ko
aarch64-linux-gnu-objdump -dr --disassemble=es8156_mute snd-soc-es8156.ko
aarch64-linux-gnu-objdump -dr --disassemble=hp_work snd-soc-es8156.ko
readelf -x .rodata snd-soc-es8156.ko
readelf -x .data snd-soc-es8156.ko
readelf -r  .rodata snd-soc-es8156.ko
```

Vendor private state (`struct es8156_priv`, 184 bytes):

| off | member | off | member |
|---|---|---|---|
| 0 | `regmap` | 32 | `debounce_time` (200 ms) |
| 8 | `dmic_amic` | 36 | `hp_det_invert` |
| 12 | `sysclk` | 40 | `work` (`delayed_work`) |
| 16 | `sysclk_constraints` | 160 | `spk_ctl_gpio` |
| 24 | `mclk` (`struct clk *`) | 164 | `hp_det_gpio` |
| | | 168 | `muted` |
| | | 169 | `hp_inserted` |
| | | 170 | `spk_active_level` |
| | | 172 | `mclk_mult` (log2) |
| | | 176 | `sclk_as_mclk` |
| | | 177 | `dll_enable` |
| | | 180 | `pwr_count` |

## Register map

`regmap_config`: `reg_bits = 8`, `val_bits = 8`, `max_register = 0xff`,
`cache_type = REGCACHE_RBTREE`, 36 register defaults.

The "default" column is `es8156_reg_defaults[]` from `.rodata+0x8f0`.
"Init" is `es8156_probe()` + `es8156_set_bias_level(STANDBY)`, "off" is
`es8156_set_bias_level(OFF)`.  Register *names* are behaviour-derived; the
datasheet was not available during the port.

| reg | default | init | off | notes |
|---|---|---|---|---|
| 0x00 | 0x1c | reset seq, then 0x02, 0x03 | - | reset / chip state machine |
| 0x01 | 0x20 | `0x21 \| mclk_mult << 6` | - | clock manager, bits 7:6 = log2(multiplier) |
| 0x02 | 0x00 | `0x04 \| sclk_as_mclk << 7` | - | bit 0 = I2S master, bit 7 = SCLK as MCLK |
| 0x03 | 0x01 | - | - | clock off |
| 0x04 | 0x00 | - | - | analog |
| 0x05 | 0x04 | - | - | channel select |
| 0x06 | 0x11 | - | - | analog |
| 0x07 | 0x00 | - | - | analog |
| 0x08 | 0x06 | 0x3f | 0x00 | analog |
| 0x09 | 0x00 | `dll_enable ? 0x02 : 0x00` | 0x02, 0x01 | DLL |
| 0x0a | 0x50 | 0x01 | - | voice base 2 |
| 0x0b | 0x50 | 0x01 | - | voice base 1 |
| 0x0c | 0x00 | - | - | voice base 0 |
| 0x0d | 0x10 | 0x14 | - | soft ramp |
| 0x0f | 0x40 (assumed) | - | - | see note on the duplicate default |
| 0x10 | 0x40 | - | - | analog |
| 0x11 | 0x00 | - | - | SDP: [2:0] format, [3] HP switch (active low), [6:4] width |
| 0x12 | 0x04 | - | - | analog |
| 0x13 | 0x11 | - | - | bit 3 = DAC digital mute |
| 0x14 | 0xbf | 0xbe | 0x00 | DAC volume, 0xbf = 191 = 0 dB |
| 0x15 | 0x00 | - | - | analog |
| 0x16 | 0x00 | - | - | analog |
| 0x17 | 0xf7 | - | - | analog |
| 0x18 | 0x00 | 0x00 | 0x01 | analog |
| 0x19 | 0x20 | - | 0x02 | analog |
| 0x1a | 0x00 | - | - | analog |
| 0x20 | 0x16 | 0x2a | - | analog control |
| 0x21 | 0x7f | 0x3c | 0x1f | analog control |
| 0x22 | 0x00 | 0x08 | 0x02 | analog control |
| 0x23 | 0x86 | 0x00 | - | analog control |
| 0x24 | 0x00 | 0x07 | - | analog control |
| 0x25 | 0x07 | 0x21, 0x01, 0x87, 0x20 | 0x21, 0x01, 0x87 | analog control |
| 0xfc | 0x00 | - | - | reserved |
| 0xfd | 0x81 | - | - | reserved |
| 0xfe | 0x55 | - | - | reserved |
| 0xff | 0x10 | - | - | reserved |

There is **no sample-rate register** in the vendor driver and none is
written on this board: the codec derives the rate from MCLK/LRCK.  The only
rate-related knobs are `everest,mclk-multiplier` (register 0x01 bits 7:6)
and `everest,sclk-as-mclk` (register 0x02 bit 7).

No ALC/DRC register block is touched by the vendor driver.

### Duplicate default for 0x10

The vendor `.rodata` table has 36 entries but lists register `0x10` twice
(`.rodata+0x960` and `+0x968`, both 0x40) and has no entry for `0x0f`.
`0x0f` is the only hole in the otherwise contiguous `0x00..0x1a` block, so
the driver uses `0x0f = 0x40` and `0x10 = 0x40`.  Marked unverified.

## Reset and power sequences (verbatim from the vendor)

```
reset:  0x00 = 0x1c
        usleep_range(5000, 5500)
        0x00 = 0x03
        0x00 = 0x3c
        usleep_range(10000, 15000)          (vendor: msleep(10))
        0x00 = 0x1c

init:   0x02 = 0x04 | sclk_as_mclk<<7
        0x20 = 0x2a
        0x21 = 0x3c
        0x22 = 0x08
        0x24 = 0x07
        0x23 = 0x00
        0x0a = 0x01
        0x0b = 0x01
        0x14 = 0xbe
        0x01 = 0x21 | mclk_mult<<6
        0x09 = dll_enable ? 0x02 : 0x00
        0x0d = 0x14
        0x18 = 0x00
        0x08 = 0x3f
        0x00 = 0x02
        0x00 = 0x03
        0x25 = 0x20

off:    0x14 = 0x00
        0x19 = 0x02
        0x21 = 0x1f
        0x22 = 0x02
        0x25 = 0x21
        0x25 = 0x01
        0x25 = 0x87
        0x18 = 0x01
        0x09 = 0x02
        0x09 = 0x01
        0x08 = 0x00
```

The mainline driver runs reset+init once in `i2c_probe()` and the `off`
sequence in the component `.remove`.  The vendor re-ran init on every
`SND_SOC_BIAS_STANDBY`; that would reset the DAC volume to 0xbe on every
playback start, so it is deliberately not copied.  Bias-level power
management is therefore not implemented; the codec stays powered and only
the optional MCLK is disabled on unbind.  This is a behaviour difference,
not a register-map difference.

## DAI

Vendor DAI name is `ES8156 HiFi`; the mainline driver uses **`es8156`** as
requested by the port plan (the name is not referenced by the board DTS).

```
playback only
stream_name   "Playback"
channels      1..2
rates         SNDRV_PCM_RATE_8000_96000
formats       S16_LE | S24_LE | S32_LE
symmetric_rate 1
```

Vendor ops: `set_fmt`, `hw_params`, `digital_mute`.  Mainline equivalent:
`set_fmt`, `hw_params`, `mute_stream`.

`set_fmt` (register 0x02 bit 0 and register 0x11 bits 2:0):

| fmt | reg 0x11 [2:0] |
|---|---|
| `SND_SOC_DAIFMT_I2S` | 0 |
| `SND_SOC_DAIFMT_RIGHT_J` | 1 |
| `SND_SOC_DAIFMT_DSP_A` | 3 |
| `SND_SOC_DAIFMT_DSP_B` | 7 |
| `SND_SOC_DAIFMT_LEFT_J` | **not supported** (vendor rejects it) |

Clock provider: `CBP_CFP` -> register 0x02 bit 0 = 1, `CBC_CFC` -> 0.
The vendor only accepts these two combinations.  Clock inversion is
rejected: the vendor's inversion switch writes the same bit 0 as the
master/slave switch (an apparent bug), so implementing it would silently
change master/slave.  The board uses `CBC_CFC` (TDM-B is bitclock/frame
master), `NB_NF`, `I2S`.

`hw_params` (register 0x11 bits 6:4):

| format | value |
|---|---|
| `SNDRV_PCM_FORMAT_S24_LE` | 0x00 |
| `SNDRV_PCM_FORMAT_S16_LE` | 0x30 |
| `SNDRV_PCM_FORMAT_S32_LE` | 0x40 |

`mute_stream`: register 0x13 bit 3.

The vendor DAI's raw `.formats` word reads `0x000000100000004e`
(bits 1, 2, 3, 6, 36) in `.data+0xb8`, which does not decode to any sane
set of `SNDRV_PCM_FMTBIT_*` values.  The DAI listed above follows the
requirement and the `set_fmt`/`hw_params` code, not that word.

## Controls

Both controls are exactly the vendor ones; `sound-name-prefix =
"Headphone"` adds the prefix.

| control | reg | shift | max | invert | private range |
|---|---|---|---|---|---|
| `DAC Playback Volume` | 0x14 | 0 | 255 | 0 | min 0, platform_max 255 |
| `Headphone Switch` | 0x11 | 3 | 1 | 1 | min 0 |

TLV (`dac_vol_tlv`, `.rodata+0x8e0`): `SNDRV_CTL_TLVT_DB_SCALE`,
min **-9550** (0.01 dB units, i.e. -95.50 dB), step **50** (0.5 dB),
no mute bit.  Therefore raw 191 (0xbf) is exactly 0 dB and raw 255 is
+32.00 dB.  The reset default is 0xbf (191), the init sequence writes 0xbe
(190, -0.5 dB).

**The ALSA maximum stays 255**, matching the vendor control and
`var/lib/alsa/asound.state` (`range '0 - 255'`, current value 193).
`/usr/lib/atrios/audio` and `rotaryd` clamp themselves to 191; 191 is the
0 dB point, not the control maximum.  There is no software attenuator in
the kernel.

With the prefix the userspace names become exactly:

```
Headphone DAC Playback Volume
Headphone Headphone Switch
```

## Jack detect

Vendor implementation:

* DT property `hp-det-gpio` on the codec node, requested with
  `of_get_named_gpio_flags()`; `devm_gpio_request_one(..., GPIOF_IN, ...)`.
* `devm_request_threaded_irq()` with
  `IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT`, handler
  queues `hp_work` after 200 ms (hardcoded `debounce_time`).
* `hp_work` reads the raw GPIO, applies `hp_det_invert` (derived from the
  DT active-low flag), stores `hp_inserted`, and drives `spk_ctl_gpio`
  (vendor property `spk-con-gpio`) if present.
* `es8156_headset_detect()` is exported for the vendor card driver.

The vendor DTS had **no** `hp-det-gpio` on the `es8156@08` node; the
property was on the card instead:
`aml-audio-card,hp-det-gpio = <&gpio 0x4d 0>`.  Vendor pin 0x4d = 77 maps
to mainline **GPIOX_11** (periphs bank offset -1).

Mainline driver:

* optional `hp-det-gpios` (`devm_gpiod_get_optional(dev, "hp-det",
  GPIOD_IN)`); the GPIO polarity comes from the DT flags, no manual
  inversion.
* 200 ms debounce work, both-edge threaded IRQ.  IRQ request failure is a
  warning, not a probe failure; without an IRQ the initial state is still
  reported once.
* Reports through an input device (`EV_SW`, `SW_HEADPHONE_INSERT`,
  name `es8156 Headphone Jack`) and, if a machine driver attaches one via
  `snd_soc_component_set_jack()`, through `snd_soc_jack_report()`
  (`SND_JACK_HEADPHONE`).  `axg-card.c` does not do that today, so the
  input device is the functional path.
* No `spk-con-gpio`: this board has no such line.  Its speakers are the
  I2C sy6045s amplifiers, so jack/amp interaction belongs to userspace.

## DT properties

| property | required | meaning |
|---|---|---|
| `compatible` | yes | `"everest,es8156"` |
| `reg` | yes | `0x08` |
| `#sound-dai-cells` | yes | `0` |
| `sound-name-prefix` | yes for userspace | `"Headphone"` |
| `hp-det-gpios` | optional | jack detect, GPIOX_11, `GPIO_ACTIVE_HIGH` per the vendor DT |
| `clocks` / `clock-names = "mclk"` | optional | MCLK; if absent, a static clock is assumed |
| `everest,mclk-multiplier` | optional | 1, 2, 4 or 8 (default 1) |
| `everest,sclk-as-mclk` | optional | route SCLK to the internal MCLK |
| `everest,dll-enable` | optional | enable the internal DLL |

No regulator properties: the vendor node had no supplies either (the
3V3_D rail is always-on).

## What is verified and what is not

Verified from the vendor binary:

* every register address/value in the reset, init and power-down sequences;
* the 36-entry defaults table and the 0x10 duplicate;
* the control register/shift/max/invert fields and the TLV numbers;
* the DAI capabilities as coded in `set_dai_fmt`/`hw_params`;
* the jack-debounce and IRQ flags;
* GPIO numbering from the DTS (vendor 77 -> GPIOX_11).

Compile-tested against the actual Linux 7.3 tree (`make
sound/soc/codecs/snd-soc-es8156.o`, also with `W=1`): clean.  Patch 0008
freshly applied with `patch -p1` and the object rebuilt from the patched
tree.  A full module link (`snd-soc-es8156.ko`) was not possible without a
complete kernel build (empty `Module.symvers`), and the module was not
loaded on hardware.

Not verified:

* Register names/semantics beyond what the vendor writes.  In particular
  0x11 bit 3 is called "Headphone Switch" because that is the vendor
  control name; it may be an output enable or a mute.
* `0x0f = 0x40` (vendor table has 0x10 twice).
* Whether the codec really derives all 8..96 kHz rates from MCLK/LRCK; no
  rate register is programmed and no PLL table exists.
* The exact meaning of `everest,mclk-multiplier` beyond "log2 written to
  register 0x01 bits 7:6".
* Jack-detect electrical polarity/debounce on the real board; the driver
  trusts `hp-det-gpios` flags and reports after 200 ms.
* Audio quality, pop behaviour, and the high-pass/ramp registers.
* The vendor DAI `.formats` word (see above).
* No ALC/DRC controls exist in the vendor driver, so none were added.
* DT binding YAML and MAINTAINERS entry are not part of patch 0008
  (checkpatch warns about MAINTAINERS; a real upstream submission needs
  `Documentation/devicetree/bindings/sound/everest,es8156.yaml`).

## Quick verification on the target

```sh
amixer -c AMLAUGESOUND contents | grep -A6 Headphone
amixer -c AMLAUGESOUND cset name='Headphone DAC Playback Volume' 191
amixer -c AMLAUGESOUND cset name='Headphone Headphone Switch' 1
aplay -D hw:0,0 -c 2 -r 48000 -f S32_LE /usr/share/sounds/alsa/Front_Center.wav
evtest                       # "es8156 Headphone Jack", SW_HEADPHONE_INSERT
i2cdetect -y 2               # 0x08 must answer
```
