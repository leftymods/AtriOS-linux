# sy6045s: Silergy SY6045S amplifier driver (Linux 7.3)

Status: **driver written, compiles, patch applies; not tested on hardware.**
Source: `drivers/audio/sy6045s.c` (staging copy), integrated by
`patches/linux/0009-soc-sy6045s.patch` as `sound/soc/codecs/sy6045s.c`.

The vendor module
`board/yandex/station2/rootfs_overlay/lib/modules/4.9.113/snd-soc-sy6045s.ko`
was built with full DWARF.  Everything below (register widths, control
registers, parser state machine, workqueue behaviour) was read back from
that module with `pahole`, `nm`, `objdump -drl --dwarf=info` and the
`.rodata`/`.data` images.  The vendor driver has no DAPM widgets; the
amplifier is a plain I2C register device with kcontrols, like
`sound/soc/codecs/tas5805m.c`.

## 1. Hardware and DTS

Two chips on the codec I2C bus (`i2c2` on the board):

| I2C address | Node | Role | Settings file |
|---|---|---|---|
| 0x2a | `audio-codec@2a` | tweeters, stereo BTL | `sy6045s-tweeters-settings.txt` |
| 0x2b | `audio-codec@2b` | woofer, PBTL (`pbtl-mode`) | `sy6045s-woofer-settings.txt` |

Both are on the TDM-B playback link.  Properties used by the driver:

```
compatible = "silergy,sy6045s";
reg = <0x2a>;
#sound-dai-cells = <0>;
sound-name-prefix = "Tweeters";           /* ALSA name prefix */
vddio-supply = <&reg_3v3d>;
pvdd-supply = <&reg_20v_amp>;
firmware-name = "sy6045s-tweeters-settings.txt";
/* legacy vendor property is also accepted: firmware = "..."; */
silergy,restore-regs = <0x07 0x01 0xff 0x06 0x01 0x08>;
pbtl-mode;                                /* woofer only */
/* optional: reset-gpios, fault-gpios */
```

`sound-name-prefix` turns `Master Playback Volume` into
`Tweeters Master Playback Volume`, which is what
`/usr/lib/atrios/audio` and `var/lib/alsa/asound.state` use.

## 2. Driver structures

```c
struct sy6045s_reg {          /* one save/restore entry */
	u32 addr;
	u32 size;             /* 1..4 bytes */
	u32 mask;
	u32 last_value;
};

struct sy6045s_priv {
	struct regmap *regmap;
	struct i2c_client *i2c;
	struct device *dev;
	unsigned int format;          /* raw dai fmt, no validation */
	const char *default_fw_name;  /* firmware-name / firmware */
	char last_fw_name[256];
	u8 fw_md5[16];
	u32 fw_crc;
	struct gpio_desc *reset_gpio; /* optional */
	struct gpio_desc *fault_gpio; /* optional, speaker enable */
	struct work_struct spk_off_work, spk_on_work;
	struct regulator_bulk_data supplies[2]; /* vddio, pvdd */
	struct mutex io_lock;         /* also the regmap lock */
	bool is_pbtl;
	struct sy6045s_reg *restore_regs;
	int restore_reg_size;
};
```

The vendor layout (`pahole -C sy6045s_priv`) is preserved field for field,
including `format` at offset 24, `last_fw_name` at 40, `fw_md5` at 296,
`fw_crc` at 312 and `is_pbtl` at 488.  The mainline code uses a
`struct device *dev` instead of relying on `codec->dev`.

## 3. Register map and transfer widths

`regmap_config`:

```c
.reg_bits = 8,
.val_bits = 32,
.max_register = 0xff,
.reg_read / .reg_write = sy6045s_reg_read / sy6045s_reg_write,
.volatile_reg = sy6045s_reg_volatile   /* reg <= 0x02 || reg == 0x0f */
.cache_type = REGCACHE_NONE,
.lock / .unlock = sy6045s_reg_lock / sy6045s_reg_unlock
```

The vendor did **not** use plain regmap I/O.  `write` builds
`[reg, value MSB..LSB]` and calls `i2c_master_send()`, `read` does a
two-message `i2c_transfer()` (register byte + N data bytes) and assembles
the result big-endian.  Single register accesses are capped at four bytes,
exactly like the original (`min(width, 4)`); longer blobs only appear in the
firmware file and are sent with `i2c_smbus_write_i2c_block_data()`.

The width table `sy6045s_reg_size[256]` is copied byte for byte from the
vendor `.rodata` (the symbol has that exact name).  A zero entry means the
register is not accessible.

| Range | Width | Notes |
|---|---|---|
| 0x00-0x2b | 1 | |
| 0x2c-0x2d | 2 | |
| 0x2e | 1 | |
| 0x2f-0x40 | 20 | |
| 0x41-0x46 | 12 | |
| 0x47-0x56 | 20 | |
| 0x57 | 12 | |
| 0x58-0x5c | 1 | |
| 0x5d-0x5e | 18 | |
| 0x5f-0x60 | 4 | |
| 0x61-0x6e | 3 | |
| 0x6f, 0x72 | 2 | |
| 0x70, 0x73-0x77, 0x7a-0x7b, 0x7f, 0x83-0x84, 0x87-0x8d, 0xa1-0xaf, 0xb1-0xcc, 0xce, 0xd0-0xf7, 0xfa | 1 | |
| 0x71, 0x79, 0x7c-0x7e, 0x82, 0x85-0x86, 0x94-0x96, 0x9a-0x9c, 0xf8-0xf9, 0xfb-0xff | 4 | |
| 0x78, 0x80-0x81, 0x8e-0x93, 0x98-0x99, 0x9d-0xa0, 0xb0 | 3 | |
| 0x97, 0xcf | 6 | |
| 0xcd | 2 | |

The ranges above are a human-readable grouping; the actual array is the
authority.  Note that `docs/audio.md` §3.2 lists `0x47-0x56` as 20 bytes;
the register *count* in that range is 16, the width is 20 bytes.

## 4. Settings file format

Real example (`sy6045s-tweeters-settings.txt`, 153 lines):

```
# compatible: sy6045s
# reset

w 54 0F 01
w 54 1B BD
w 54 82 00 00 00 F0
...
# Unmute
w 54 22 00
w 54 06 00
```

Grammar (reconstructed from `sy6045s_apply_settings`):

* `w <cmd> <byte>...`
  * exactly one space after `w`,
  * `<cmd>` is two hex digits and is sent as the **SMBus block command
    byte** (`block[0]`).  In the shipped files it equals the 8-bit I2C
    address (`54` = 0x2a<<1, `56` = 0x2b<<1), but the driver never
    compares it with the client address;
  * each following token is exactly one space plus two hex digits; the
    first of them is the register on the chip's block-write interface;
  * an optional `\r` is accepted at end of line;
  * the command and all following bytes are collected into one pending
    block.
* `> <byte>` appends one byte to the pending block (multi-byte DSP writes,
  e.g. 20-byte coefficient blocks).
* `# ...` and `d ...` are comments; an empty line and CRLF are ignored.
* A comment, blank line, next `w`, or EOF flushes the pending block with
  `i2c_smbus_write_i2c_block_data(command = block[0],
  len = block_len - 1, data = block[1..])`.
* At most 32 data bytes fit in one block (I2C SMBus limit); a register is
  mandatory, so the block buffer is 33 bytes.
* Any other first character, bad hex, a `>` without a pending block, or a
  line shorter than 10 characters is rejected with
  `Error parsing settings at line %d`.

The file has **no checksum tail**.  After a successful parse the driver
computes:

* `fw_md5 = md5(entire file)` (vendor used `crypto_alloc_shash("md5")`,
  this driver uses the same algorithm from `lib/crypto/md5.c`);
* `fw_crc = ~crc32_le(~0, entire file)`;
* `last_fw_name = firmware file name`.

They are reported by the `settings_file` sysfs attribute as
`<name> <16 md5 bytes> <crc32>`.  There is nothing to compare them against;
they exist so userspace can tell which sequence is loaded.

The vendor only checked the `# compatible: sy6045s` marker in the
`settings_file` sysfs path, and required it at the **start** of the file
(`strstr(data, marker) == data`).  This driver does the same; the probe
path applies the file without the marker check, matching the vendor.

## 5. Controls

All names are the vendor names; `sound-name-prefix` prepends the chip name.

| Control | Register | Field | Type |
|---|---|---|---|
| `Master Playback Volume` | 0x07 | 8 bits, 0..255 | TLV, min -127.5 dB, step 0.5 dB, mute |
| `Ch1 Playback Volume` | 0x08 | 8 bits, 0..255 | TLV, min -79.5 dB, step 0.5 dB, mute |
| `Ch2 Playback Volume` | 0x09 | 8 bits, 0..255 | TLV, min -79.5 dB, step 0.5 dB, mute |
| `Master Playback Switch` | 0x06 | bit 3 | boolean |
| `Ch1 Playback Switch` | 0x06 | bit 0 | boolean |
| `Ch2 Playback Switch` | 0x06 | bit 1 | boolean |
| `EQ Switch` | 0x04 | bit 4 | boolean |
| `DRC Switch` | 0x60 | bits 0..3, all ones = on | boolean, custom get/put |

The volume limits are exactly the vendor `soc_mixer_control` values:
`min=0, max=255, platform_max=255, reg/rreg` as above, `shift=0`.
The master TLV minimum is -127.5 dB (`0xffffce32` in `.rodata`), the
Ch1/Ch2 minimum is -79.5 dB (`0xffffe0f2`); both use step 50 (0.5 dB) and
the mute flag.  `docs/audio.md` and the task text mention -79.5 dB for all
three; the binary has two different tables, and the driver keeps both.

`DRC Switch` uses the vendor `struct sy6045s_reg_switch`
`{reg, mask, on_value, off_value}` with `reg=0x60, mask=0x0f, on=0x0f,
off=0`.  A boolean ALSA control reports "on" when
`(reg & mask) != off_value`.

## 6. DAI and hw_params

```
DAI name : "sy6045s-hifi"
playback : 1..2 channels, S32_LE, 8..96 kHz
ops      : set_fmt, hw_params, trigger
```

`set_dai_fmt()` stores the raw format and returns 0 without validation,
like the vendor.  `hw_params()` writes register 0x15:

```
val = 0
switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
  IB_NF -> 0x20
  IB_IF -> 0x40
  else  -> 0
}
switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
  RIGHT_J -> 0x08
  LEFT_J  -> 0x04
  I2S     -> (no extra bit, "i2s" flag set)
}
switch (params_format()) {
  S16_LE -> 0x03
  S24_LE -> 0x01
  S32_LE -> (nothing)
}
if (!i2s)
  val |= 0x10
write reg 0x15 = val
```

The board uses `dai-format = "i2s"` / 32-bit slots, so the produced value
is `0x00` (S32_LE + I2S + NB_NF).  The 0x200/0x300/0x400 comparisons in
the vendor binary are 4.9 `SND_SOC_DAIFMT_*` constants; Linux 7.3 changed
the inversion constants to `IB_NF = 0x300`, `IB_IF = 0x400`, so the
driver uses the current macros rather than the old numeric values.

## 7. Speaker enable/disable

The vendor means "mute the amplifier output" with bit 4 of register 0x22:

* on playback `START`/`RESUME`/`PAUSE_RELEASE`: clear bit 4
  (`SPK ON (queue work)`); if a `fault-gpios` is described, drive it high
  (`SPK ON (gpio)`) instead of queueing work;
* on `STOP`/`SUSPEND`/`PAUSE_PUSH`: set bit 4
  (`SPK OFF (queue work)`), or drive the GPIO low (`SPK OFF (gpio)`);
* `sy6045s_set_sby_mode_on()` (probe/remove) logs `SPK OFF (update bits)`
  and sets bit 4 when no `fault` GPIO exists.

The two work items run on `system_highpri_wq`, as in the vendor driver,
and call `regmap_update_bits(0x22, BIT(4), ...)`.

Note: the vendor `trigger` decoding maps `SUSPEND` to the "on" path and
`RESUME`/`PAUSE_RELEASE` to "off".  That looks reversed; this driver uses
the standard ASoC meaning (START/RESUME/PAUSE_RELEASE = on,
STOP/SUSPEND/PAUSE_PUSH = off).  On this board there is no `fault` GPIO,
so the only visible effect is bit 4 of 0x22.

## 8. Probe flow

1. allocate private data, regmap (`lock_arg = priv`), works, mutex;
2. read `firmware-name`, else `firmware`, else none;
3. read `pbtl-mode`;
4. request and enable `vddio`/`pvdd`;
5. optional `reset-gpios`: 20 ms, release (set 1), 20 ms;
6. optional `fault-gpios`;
7. parse `silergy,restore-regs`, else `restore-regs`;
8. `snd_soc_register_component()`;
9. load the settings file.  **Failure is only a warning**; probe still
   succeeds and the chip keeps its power-on defaults;
10. program BTL (`0x1e = 0x05`, `0x20 = 0x00`) or PBTL
    (`0x1e = 0x07`, `0x20 = 0x80`);
11. `sy6045s_set_sby_mode_on()`.

Remove cancels both works, forces the speaker off, unregisters the
component and disables the regulators.

### restore-regs

The vendor property is a flat array of `(addr, size, mask)` triplets:

```
silergy,restore-regs = <0x07 0x01 0xff 0x06 0x01 0x08>;
```

`sy6045s_save_regs()` reads `size` bytes into `last_value`;
`sy6045s_restore_regs()` reads the current value, computes
`(current & ~mask) | (last & mask)` and writes it back.  They are used by
the `settings_file` and `default_settings` sysfs re-apply paths, exactly
as in the vendor driver.  `size > 4` is rejected.

## 9. sysfs interface

The driver exposes the vendor attributes on the I2C device:

| Attribute | Access | Meaning |
|---|---|---|
| `settings` | write | apply raw text settings |
| `settings_file` | read/write | read `<name> <md5> <crc>`; write a file name to re-apply it |
| `default_settings` | write | save regs, reset, re-apply the DT firmware, restore regs |

`settings_file` and `default_settings` take `io_lock`, call
`save_regs()`, `reset_chip()` (`i2c_smbus_write_byte_data(0x0f, 0x01)`,
20 ms sleep), `load_firmware()` and `restore_regs()`.

## 10. Verification performed

* `make O=/tmp/kbuild-sy ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
   M=/tmp/sy-mod sy6045s.o` against a 7.3 tree: **compiles with no
  warnings**.
* Kconfig/Makefile integration: with `CONFIG_SND_SOC_SY6045S=m`,
  `make O=/tmp/kbuild-sy ARCH=arm64 ... \
   sound/soc/codecs/snd-soc-sy6045s.ko` compiled and linked the module.
  The tree was only `modules_prepare`d, so modpost could not resolve core
  symbols; `KBUILD_MODPOST_WARN=1` was used to finish the `.ko`.  The
  unresolved symbols are an artefact of the partial build tree, not of the
  driver.
* The parser was replayed over both shipped settings files (153 lines,
  117 block writes each, maximum payload 21 bytes = register + 20-byte
  coefficient block); no line was rejected.
* `git apply --check` and `patch --dry-run -p1` of
  `patches/linux/0009-soc-sy6045s.patch` against a pristine copy of the
  7.3 `sound/soc/codecs/Kconfig`, `Makefile` and an empty
  `sy6045s.c` path: **OK**.  The shared
  `/home/leftymods/mainline-src/linux-7.3` tree already had the patch
  applied by the integration process while this work was in progress; it
  was not modified or reverted here.
* `scripts/checkpatch.pl --no-tree --terse 0009-soc-sy6045s.patch`:
  **0 errors, 1 warning**.  The warning is `CONST_STRUCT` on the
  per-device `struct regmap_config` copy, which is non-const because
  `lock_arg` must point at the private data.  A static const config cannot
  carry a per-instance lock argument.

## 11. Not verified / risks

1. **No hardware test.**  Nothing was run on a Station Max; register
   values, the BTL/PBTL writes, the TDM-B clock setup and the actual
   settings files are untested on mainline 7.3.
2. **MCLK differs from the vendor kernel.**  The settings files were
   tuned for the vendor 24.576 MHz/512x setup; mainline derives MCLK =
   `mclk-fs * rate` (12.288 MHz at 48 kHz).  If the amps stay silent,
   check MCLK on a scope before editing the settings files
   (`docs/audio.md` §8.2).
3. **DAIFMT constants changed between 4.9 and 7.3.**  The driver uses the
   7.3 macros.  Only the board's I2S/NB_NF path is meaningful; the old
   vendor 0x200/0x400 numeric cases cannot be reproduced literally.
4. **Vendor trigger mapping is odd** (SUSPEND treated as "on").  The port
   uses standard ASoC semantics; see §7.
5. **Two TLVs.**  The master control uses -127.5 dB, Ch1/Ch2 -79.5 dB,
   as in the vendor binary.  The task text mentions -79.5 dB for all;
   changing the master TLV would alter the userspace volume curve.
6. **The driver copy in `drivers/audio/sy6045s.c` is a staging copy.**
   `scripts/apply.sh` copies `drivers/**` to `drivers/...`, so it
   will also install `drivers/audio/sy6045s.c` (unreferenced).  The real
   integration is the patch, which puts the file in
   `sound/soc/codecs/`.  If apply.sh is used, either ignore the extra
   `drivers/audio/` copy or add it to the script's exclude list.
7. **`settings_file` marker check is strict** (marker must be at byte 0),
   matching the vendor.  A file with a leading BOM or blank line is
   rejected by the sysfs re-apply path.
8. **Register writes are capped at 4 bytes**, like the vendor
   `sy6045s_reg_write`.  Long coefficient blocks are only written by the
   firmware parser.  A future `regmap_raw_write()` path would need the
   width table extended past the 4-byte cap.
9. **No DAPM.**  The amp is always-on from DAPM's point of view; the
   vendor had no widgets either.  The 20 V rail is always-on on this
   board, so the mute sequence (register 0x22 bit 4) is the only pop
   protection.
10. **`fault` GPIO naming.**  The vendor calls the optional output
    "fault", but drives it as a speaker enable (high = on).  Keep that
    meaning when adding `fault-gpios` to a board DTS.
