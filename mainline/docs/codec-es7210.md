# ES7210 microphone ADC: mainline driver

`sound/soc/codecs/es7210.c` (copy: `mainline/drivers/audio/es7210.c`,
patch: `mainline/patches/linux/0007-soc-es7210.patch`) is a new ASoC driver
for the Everest Semiconductor ES7210 4-channel ADC that is the microphone
array front end of the Yandex Station Max (Amlogic S905X3 / SM1).

Linux 7.3 has no `es7210.c` and no `SND_SOC_ES7210`
(`sound/soc/codecs/` only has `es7134`, `es7241`, `es8311/16/23/26/28/75/89`,
`es83xx`, `es9356`), so the driver was ported from the vendor 4.9.113
module, which ships with full DWARF.

| item | value |
|---|---|
| I2C address | 0x40 (vendor DT `es7210@40`, `everest,es7210_2ch`) |
| bus | vendor `i2c@1d000` = mainline `i2c2` (vendor 300 kHz, audio DTS fragment 100 kHz) |
| direction | capture only, SDOUT -> TDM-B `din0` (GPIOAO_4) |
| DAI name | `es7210` |
| controls prefix | `Loopback` (`sound-name-prefix`) |
| Kconfig | `SND_SOC_ES7210`, `depends on I2C`, `select REGMAP_I2C` |
| module | `snd-soc-es7210.ko` |
| regmap | 8/8 bit, `max_register` 0x4c, `REGCACHE_RBTREE` |

The vendor node has no `clocks`, no `reset-gpios` and no supplies; the
mainline driver therefore treats MCLK as a fixed 256 fs clock unless a
`mclk` clock is described in DT, and reset is optional.

## 1. Evidence: what was read out of `snd-soc-es7210.ko`

Source path recorded in DWARF:
`/place/sandbox-data/tasks/3/9/1984047393/quasar/out/target/product/yandexstation_2/obj/vendor_modules/es7210/es7210.c`
(one compilation unit, `DW_AT_comp_dir` stripped).

`readelf --debug-dump=info`, `pahole`, `nm`, `readelf -r` and
`aarch64-linux-gnu-objdump -dr` gave:

| symbol | type | DWARF decl line | location |
|---|---|---|---|
| `es7210_channel_type` enum (`ES7210_2CH` = 2, `ES7210_4CH` = 4) | enum | 31 | - |
| `struct es7210_priv` | struct, 168 B | 32 | - |
| `es7210_regmap_config` | const struct regmap_config | 43 | `.rodata+0x40` |
| `struct es7210_reg_config` (`reg_addr`, `reg_v`, both u8) | struct, 2 B | 49 | - |
| `es7210_reg_defaults[]` | 52 x 2 B | 55 | `.rodata+0x118` |
| `es7210_tdm_reg_common_cfg[]` | 21 x 2 B | 71 | `.rodata+0x190` |
| `es7210_set_dai_sysclk` | fn, returns 0 | 83 | `.text+0x000` |
| `es7210_set_dai_fmt` | fn | 90 | `.text+0x180` |
| `es7210_tdm_init_codec` | fn, inlined | 153 | inlined into `es7210_probe` |
| `es7210_unmute` | fn, inlined | 188 | inlined into the work handler |
| `pcm_pop_work_events` | fn | 195 | `.text+0x0e8` |
| `es7210_mute` | fn, inlined + out of line | 203 | `.text+0x5e0` |
| `es7210_pcm_startup` | fn | 220 | `.text+0x090` |
| `es7210_pcm_hw_params` | fn, returns 0 | 228 | `.text+0x020` |
| `es7210_ops` | struct snd_soc_dai_ops | 235 | `.rodata+0x780` |
| `es7210_dai0` | struct snd_soc_dai_driver | 243 | `.data+0x000` |
| `es7210_suspend` / `es7210_resume` | fn | 258 / 263 | `.text+0x8d8` / `+0x360` |
| `es7210_probe` (component probe) | fn | 269 | `.text+0x6a0` |
| `es7210_remove` | fn, returns 0 | 295 | `.text+0x040` |
| `es7210_set_bias_level` | fn | 300 | `.text+0x390` |
| `mic_boost_tlv` / `adc_gain_tlv` | 4 x u32 each | 335 / 336 | `.rodata+0x848` / `+0x838` |
| `es7210_snd_controls[]` | 12 x `snd_kcontrol_new` | 337 | `.rodata+0x478` |
| `soc_codec_dev_es7210` | struct snd_soc_codec_driver | 352 | `.data+0xe8` |
| `es7210_i2c_probe` | fn | 365 | `.text+0x478` |
| `es7210_i2c_remove` | fn | 403 | `.text+0x060` |
| `es7210_i2c_id[]` / `es7210_dt_ids[]` | - | 409 / 417 | `.rodata+0x418` / `+0x1c0` |

`struct es7210_priv` (`pahole -C es7210_priv`) - all fields accounted for:

```
regmap 0, codec 8, dmic_enable 16, sysclk 20, mclk 24,
sysclk_constraints 32, channels 40 (u8), pcm_pop_work 48 (delayed_work)
```

The DAI ops table (`.rodata+0x780`, relocations) has exactly five non-NULL
members: `set_sysclk` (+0), `set_fmt` (+0x20), `digital_mute` (+0x48),
`startup` (+0x58), `hw_params` (+0x68). There is **no** `set_tdm_slot`, no
`hw_free`, no `prepare`, no `trigger`. The out-of-line `hw_params` and
`set_sysclk` bodies are `return 0`.

`regmap_config` (raw bytes): `name = NULL`, `reg_bits = 8`,
`val_bits = 8`, `max_register = 0x4c`, `cache_type = REGCACHE_NONE`,
no `reg_defaults`. The 52-entry table is written by hand in `i2c_probe`.

## 2. Register map

Names are from the Everest BSP header (`es7210.h`, the same names the
vendor module's register writes imply); the datasheet was not available.
"default" is the value written by the vendor `i2c_probe()`, "TDM init" the
value the vendor component probe writes afterwards.

| addr | name | default | TDM init | used in driver for |
|---|---|---|---|---|
| 0x00 | RESET_CTL | 0x32 | 0x41 | reset/power-up |
| 0x01 | CLK_ON_OFF | 0x20 | - | clock on/off |
| 0x02 | MCLK_CTL | 0x02 | 0xc1 | MCLK/LRCK ratio: 0xc3=768, 0xc1=256, 0x41=64, 0x01=128 |
| 0x03 | MST_CLK_CTL | 0x04 | - | master clock (unused, slave) |
| 0x04 | MST_LRCDIVH | 0x01 | - | LRCK divider (unused, slave) |
| 0x05 | MST_LRCDIVL | 0x00 | - | LRCK divider (unused, slave) |
| 0x06 | DIGITAL_PDN | 0x00 | 0x04 | digital power down |
| 0x07 | ADC_OSR | 0x20 | 0x20 | oversampling ratio |
| 0x08 | MODE_CFG | 0x10 | - | [7:4] channels/2, [3] BCLK invert*, [0] frame master* |
| 0x09 | TCT0_CHPINI | 0x40 | 0x30 | chopper/analog timing |
| 0x0a | TCT1_CHPINI | 0x40 | 0x30 | chopper/analog timing |
| 0x0b | CHIP_STA | 0x00 | - | chip status |
| 0x0c | IRQ_CTL | 0x00 | - | interrupt control (unused) |
| 0x0d | MISC_CTL | 0x09 | - | misc |
| 0x10 | DMIC_CTL | 0x00 | - | digital mic mode (unused) |
| 0x11 | SDP_CFG1 | 0x00 | - | [7:5] word length, [4:0] serial format |
| 0x12 | SDP_CFG2 | 0x00 | - | 0x02 I2S/left-J/DSP-A, 0x01 DSP-B |
| 0x13 | ADC_AUTOMUTE | 0x00 | - | automute (unused) |
| 0x14 | ADC34_MUTE | 0x00 | - | bit0 ADC3, bit1 ADC4 (1 = mute) |
| 0x15 | ADC12_MUTE | 0x00 | - | bit0 ADC1, bit1 ADC2 (1 = mute) |
| 0x16-0x1a | ALC_SEL .. ALC_COM_CFG2 | 0x00 | - | ALC config |
| 0x1b | ALC4_MAX_GAIN | 0xbf | - | "ADC4_MAX_GAIN" control, 0..255 |
| 0x1c | ALC3_MAX_GAIN | 0xbf | - | "ADC3_MAX_GAIN" control, 0..255 |
| 0x1d | ALC2_MAX_GAIN | 0xbf | - | "ADC2_MAX_GAIN" control, 0..255 |
| 0x1e | ALC1_MAX_GAIN | 0xbf | - | "ADC1_MAX_GAIN" control, 0..255 |
| 0x18/0x19 | ALC34_LVL / ALC12_LVL | 0xf7 | - | ALC levels |
| 0x20/0x21 | ADC34_HPF2 / HPF1 | 0x26 | 0x26 | high-pass filter coefficients |
| 0x22 | ADC12_HPF2 | 0x06 | 0x06 | high-pass filter coefficients |
| 0x23 | ADC12_HPF1 | 0x26 | 0x26 | high-pass filter coefficients |
| 0x3d/0x3e | CHIP_ID1 / CHIP_ID0 | 0x72 / 0x10 | - | chip id (written, never checked) |
| 0x3f | CHIP_VER | 0x00 | - | version |
| 0x40 | ANALOG_SYS | 0x80 | 0xc3 | analog bias / reference |
| 0x41 | MICBIAS12 | 0x71 | 0x70 | mic bias ADC1/2 |
| 0x42 | MICBIAS34 | 0x71 | 0x70 | mic bias ADC3/4 |
| 0x43-0x46 | MIC1..4_GAIN | 0x00 | 0x10 | PGA: [3:0] gain, bit4 PGA enable* |
| 0x47-0x4a | MIC1..4_LP | 0x00 | 0x08 | MIC low-power / bias current |
| 0x4b | MIC12_PDN | 0xff | - | 0x00 = ADC1/2 powered (2ch: 0xff = 1/2 off) |
| 0x4c | MIC34_PDN | 0xff | - | 0x00 = ADC3/4 powered (2ch: 0xff = 3/4 off) |

`*` bit meaning inferred from the vendor code paths; not confirmed against
a datasheet.

### Channel count (vendor `es7210_probe`, lines 269-293)

`MODE_CFG[7:4] = channels / 2`, i.e. `0x10` for the 2-channel variant
(`everest,es7210_2ch`, the one in the vendor DT) and `0x20` for 4 channels.
Additionally, the 2-channel build writes `MIC12_PDN = 0x00` and
`MIC34_PDN = 0xff` (ADC3/4 powered down), the 4-channel build
`0x00`/`0x00`. The mainline driver performs exactly the same three writes
(`es7210_set_channels()`), both at probe (from the compatible) and again in
`hw_params()` from the PCM channel count, so a 2-channel capture really
switches ADC3/4 off and a 4-channel one switches them back on. Note that
the reset value of both PDN registers in the vendor table is `0xff`, so
omitting this step would leave the whole array powered down.

### Serial format (vendor `es7210_set_dai_fmt`, line 90)

The vendor driver writes the whole byte `SDP_CFG1` and `SDP_CFG2`:

| `fmt & SND_SOC_DAIFMT_FORMAT_MASK` | SDP_CFG1 | SDP_CFG2 |
|---|---|---|
| `SND_SOC_DAIFMT_I2S` | 0x60 | 0x02 |
| `SND_SOC_DAIFMT_LEFT_J` | 0x61 | 0x02 |
| `SND_SOC_DAIFMT_DSP_A` | 0x63 | 0x02 |
| `SND_SOC_DAIFMT_DSP_B` | 0x73 | 0x01 |
| anything else | -EINVAL | - |

`SDP_CFG1[7:5]` is the word length (`0x60` here = 16 bit). The mainline
driver keeps the vendor values but updates only `[4:0]` in `set_fmt` and
lets `hw_params` own `[7:5]`, so a re-run of `set_fmt` cannot clobber the
sample width.

Word length written by `hw_params` (vendor `es7210_pcm_hw_params`, line 228,
in the Rockchip/Allwinner variant of the same driver; the Station module
stubs `hw_params` out):

| format | `SDP_CFG1[7:5]` |
|---|---|
| S16_LE | 0x60 |
| S24_LE / S32_LE | 0x80 |
| (S24_3LE / S20_3LE, not advertised here) | 0x00 / 0x20 |

Master/slave and polarity in `MODE_CFG`: the vendor sets bit 0 for the
"codec provides the frame clock" case (`0x2000` in its numbering) and bit 3
for `IB_NF`/`IB_IF`; everything else is rejected. Mainline names
(`SND_SOC_DAIFMT_CBx_CFx`) map numerically to the same values, so the
driver keeps that mapping for all four clock-provider combinations. The
station runs the codec as a full consumer (`CBC_CFC`, vendor DT
`bitclock-master`/`frame-master` = TDM-B), i.e. bits 0 and 3 stay 0.

Vendor quirk worth knowing: its `set_dai_fmt` rejects `fmt & 0xf000 == 0`,
which is exactly `CBS_CFS`/`CBC_CFC`, the mode the vendor machine driver
passes. That is a bug in the vendor module (the error is only logged by
soc-core), not something to copy.

## 3. Controls (identical names/ranges/TLVs to the vendor)

Vendor table `es7210_snd_controls[]` (12 entries, `.rodata+0x478`), each
`private_value` is a `struct soc_mixer_control` compound literal in
`.data+0x2a0 .. 0x718`:

| name | reg | shift | max | access | TLV |
|---|---|---|---|---|---|
| `PGA1_setting` | 0x43 | 0 | 0x0f | RW + TLV_READ | `mic_boost_tlv` |
| `PGA2_setting` | 0x44 | 0 | 0x0f | RW + TLV_READ | `mic_boost_tlv` |
| `PGA3_setting` | 0x45 | 0 | 0x0f | RW + TLV_READ | `mic_boost_tlv` |
| `PGA4_setting` | 0x46 | 0 | 0x0f | RW + TLV_READ | `mic_boost_tlv` |
| `ADC1_MUTE` | 0x15 | 0 | 1 | RW | - |
| `ADC2_MUTE` | 0x15 | 1 | 1 | RW | - |
| `ADC3_MUTE` | 0x14 | 0 | 1 | RW | - |
| `ADC4_MUTE` | 0x14 | 1 | 1 | RW | - |
| `ADC1_MAX_GAIN` | 0x1e | 0 | 0xff | RW + TLV_READ | `adc_gain_tlv` |
| `ADC2_MAX_GAIN` | 0x1d | 0 | 0xff | RW + TLV_READ | `adc_gain_tlv` |
| `ADC3_MAX_GAIN` | 0x1c | 0 | 0xff | RW + TLV_READ | `adc_gain_tlv` |
| `ADC4_MAX_GAIN` | 0x1b | 0 | 0xff | RW + TLV_READ | `adc_gain_tlv` |

TLV arrays, raw words:

```
adc_gain_tlv  = { 1, 8, 0xffffdae4, 50 }  -> min -9500 (=-95.00 dB), step 50 (0.5 dB), no mute
mic_boost_tlv = { 1, 8, 0,          300 } -> min 0, step 300 (3.0 dB), no mute
```

so `ADCn_MAX_GAIN` is -95.00 .. +32.50 dB and `PGAn_setting` is
0.00 .. +45.00 dB (raw 15 x 3 dB). Both are plain `SOC_SINGLE_TLV`, no
inversion, no software volume: the station's userspace
(`/usr/lib/atrios/audio`) owns the gain curve and only writes these raw
values. Reset value of all four gain registers is 0xbf (= +0.5 dB with
this TLV), which is what the vendor leaves in place.

## 4. DAI

```
name            es7210                 (vendor: "ES7210 ADC 2/4CH")
capture         stream "Capture", 2..4 channels
rates           SNDRV_PCM_RATE_8000_48000
formats         S16_LE | S24_LE | S32_LE
symmetric_rate  1
ops             set_fmt, set_tdm_slot, hw_params, mute_stream
```

Vendor DAI: `.data+0` = `{ name = "ES7210 ADC 2/4CH", ops = .rodata+0x780,
capture = { stream_name = "Capture", formats = 0x1000000444, rates = 0x7fe,
channels_min = 2, channels_max = 4 }, symmetric_rates = 1 }`. Formats
`0x444` = S16_LE|S24_LE|S32_LE plus bit 36 (`SNDRV_PCM_FMTBIT_S20_3LE`);
rates `0x7fe` = 11025 .. 176400 Hz. The driver advertises the three formats
the port asked for and 8..48 kHz (the mclk-fs=256 link and the vendor's
11025..176400 Hz advertisement are wider than the board ever uses).

`set_tdm_slot` accepts 2, 4 and 8 slots and slot widths 16/24/32 (0 =
unspecified) and records them; `hw_params` refuses more channels than
slots. Nothing is written to the chip: the ES7210 has no slot-count
register, the multi-LRCK TDM variants in the vendor BSP only change
`SDP_CFG2` to 0x07 (first chip) / 0x03 (following chips), and this board
is a plain 2-slot link.

## 5. Mute / pop handling

* `es7210_init()` leaves both `ADC12_MUTE` and `ADC34_MUTE` at 0x03, so
  the ADC is silent until a stream starts.
* `mute_stream()` mirrors the vendor `digital_mute()`: 0x03 on stop, 0x00
  on start (called from `soc_pcm_prepare()` for capture as well).
* The vendor additionally queued `pcm_pop_work` 25 jiffies after
  `startup()` to unmute (`pcm_pop_work_events`, line 195) and logged
  `enter into %s` lines. That work is *not* ported: the mainline DAPM/PCM
  core already unmutes in `prepare`, and the extra 100 ms mute would only
  delay the first samples. If a DC pop shows up on hardware, this is the
  first thing to restore.

## 6. DT / userspace contract

`docs/audio-dts-fragment.dts` already contains the node:

```dts
es7210: audio-codec@40 {
	compatible = "everest,es7210";
	reg = <0x40>;
	#sound-dai-cells = <0>;
	sound-name-prefix = "Loopback";
	status = "okay";
};
```

* `everest,es7210` = 4 channels, `everest,es7210_2ch` = 2 channels (vendor
  compatible, also accepted), `everest,es7210_4ch` = 4 channels.
* optional `reset-gpios` (asserted for 10 ms at probe) and `clocks`/`mclk`
  (name `mclk`); the vendor DT has neither.
* `sound-name-prefix = "Loopback"` turns the controls into
  `Loopback ADC1_MUTE`, `Loopback ADC1_MAX_GAIN`, ... which is what
  `/usr/lib/atrios/audio` and `var/lib/alsa/asound.state` expect.
* No supplies are modelled; the codecs' 3V3_D rail is always-on in the
  vendor DT.

## 7. Verification performed

Compiled against a real Linux 7.3 tree (unpacked tarball,
`aarch64-linux-gnu-gcc` 12, arm64 defconfig + `modules_prepare` in a
separate `O=` directory):

```
make -C linux-7.3 O=$O ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     M=$moddir W=1 es7210.o        # CC [M] es7210.o, no warnings
```

with the file placed as `sound/soc/codecs/es7210.c` and the patch applied
via `git apply` and `patch -p1` in throw-away trees (both apply cleanly;
`patch -p1 -R --dry-run` succeeds, so `scripts/apply.sh`'s idempotency
check works).

`scripts/checkpatch.pl --no-tree --strict` (upstream master copy, with
`spelling.txt` and `const_structs.checkpatch`):

* driver file: 0 errors, 0 warnings, 0 checks (728 lines)
* patch 0007: only the generic "added, moved or deleted file(s), does
  MAINTAINERS need updating?" warning, which is expected for a new file.

Not run: a real build of the whole kernel/module with modpost (no
`Module.symvers` in the throw-away tree), and no run on the device.

## 8. Not verified / risks

1. **No hardware test.** Nothing here has been run on a Station Max.
   Expected capture path: `arecord -D hw:0,1 -c 2 -r 48000 -f S32_LE`.
2. **`MODE_CFG` bits 0 and 3** (frame master, BCLK invert) are inferred
   from the vendor's `set_fmt`; only the all-zero (slave, normal polarity)
   combination matters on this board and that is what the link uses.
3. **`MICn_GAIN` bit 4** is assumed to be a PGA enable because the vendor
   TDM table writes 0x10 (= gain 0) where the BSP variant writes 0x1E
   (gain 14). If 0x10 is wrong, all four PGAs stay muted - compare with
   `amixer -c AMLAUGESOUND cget name='Loopback PGA1_setting'` plus the
   actual gain registers via `i2cget`.
4. **ADC3/4 power-down.** The vendor 2-channel build powers ADC3/4 down
   (`MIC34_PDN = 0xff`) and the station's vendor DT uses the 2-channel
   compatible, while the hardware is a 4-mic array. The driver follows the
   compatible/PCM channel count; if the array is really 4 channels the DT
   has to use `everest,es7210` (or `everest,es7210_4ch`) and the PCM has to
   open with 4 channels, otherwise ADC3/4 stay off. If the 8-channel array
   at 0x43 exists, both chips have to be brought up together and the PDN
   mapping re-checked.
5. `ADCn_MAX_GAIN` maps to the ALC max-gain registers (0x1b..0x1e) exactly
   as the vendor does; the name is misleading (it is not the digital
   volume) but it is load-bearing for userspace.
6. `CHIP_ID1/ID0` are written, never read back - no chip-id check, so a
   wrong/absent chip is only detected by I2C write failures.
7. `resume` in the vendor just calls `regcache_sync()` (a no-op with
   `REGCACHE_NONE`); the mainline driver has no PM callbacks. On this board
   the codec is never powered down, so this is untested and probably fine,
   but a suspend/resume cycle is unhandled.
