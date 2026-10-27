# DTS/DTS Port Comparison — Yandex Station Max (S905X2 → S905X3)

Date: 2026-10-24
Ported tree: `tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts`
Original tree: stock S905X2 DTS (`amlogic-dt-id = "g12a_s905x2_rev375f"`)
Reference tree: `common/arch/arm64/boot/dts/amlogic/sm1_s905x3_ac214.dts`

## Method

Both files are decompiled DTBs with numeric phandles and different formatting, so a
plain textual `diff` is useless. The comparison was done with a semantic DTS parser
(`/tmp/dtsdiff.py`) that:

1. tokenizes nodes and properties,
2. normalizes cell values and phandle references,
3. reports removed/added nodes and per-property differences on common nodes.

Result: **orig 575 nodes / port 622 nodes / 461 common / 114 only-orig /
161 only-port / 159 common nodes with property differences.**

## Limitation: the stock S905X3 DTB cannot be decompiled

`yandex_1.99.img` ships `meson1.dtb`, `meson1_ENC.dtb` and `_aml_dtb.PARTITION`
as the **same 128,032-byte blob** with entropy 7.999 and no `AML_` / `d00dfeed`
magic. U-Boot's `store_interface.c` calls
`aml_sec_boot_check(AML_D_P_IMG_DECRYPT, ...)` on it, i.e. it is encrypted with the
Amlogic secure-boot key held in the SoC eFuse. It therefore cannot be decrypted or
decompiled offline.

A plaintext DTB *does* exist in the image, but it is only a 374-byte DTBO
(`dummy-battery` / `dummy-charger`), not the board DTB. The real comparison
baseline is the S905X2 DTS the port was derived from, which is what this report uses.

## Real defects found and fixed

| # | Defect | Fix |
|---|--------|-----|
| 1 | `/unifykey` declared 20 keys (`unifykey-num = <0x14>`) but only had `unifykey-index-0..17`; `key_18`/`key_19` had no `unifykey-index-N` and no `phandle`. | Added `unifykey-index-18 = <0x11e>`, `unifykey-index-19 = <0x11f>` and `phandle = <0x11e>/<0x11f>` on `key_18`/`key_19`. |
| 2 | `overlay/boot/cmdline.txt` passed `androidboot.hardware.amlogic_dt_id=g12a_s905x2_rev376f`, which does not match the DTS id `yandex_station_2g`; U-Boot's multi-DTB matcher would not select the ported DTB. | Changed to `yandex_station_2g`; `build_atrios.sh` now re-syncs it on every build. |
| 3 | `dts_boot.compile_dts()` silently wrote a 256-byte mock DTB when no `dtc` was found. | Hard-fails instead. DTC is discovered in `prebuilts/misc/linux-x86/dtc/dtc`. |
| 4 | `build_multi_dtb()` ran `dtbTool` on the whole staging directory, so `meson1.dtb`/`meson1_ENC.dtb` were packed twice and a stray `sm1_s905x3_yandex.dtb` was shipped. | Packs from an isolated `.dtb_pack_src/` directory; intermediate DTB moved to `.fw_build_work/`. |
| 5 | `dtbTool` shells out to a bare `dtc`; without the DTC directory on `PATH` it failed and the fallback copied a raw DTB. | `_tool_env()` prepends the DTC directory to `PATH`. |

Verified after the fix: the compiled blob is a real 93,215-byte DTB and the
multi-DTB container is a valid 96,256-byte `AML_` v2 image containing the DTB at
offset 0x800 under the id `yandex station 2g`.

## Intentional / correct SM1 port changes (not defects)

### SoC and clocks
- CPU OPP table `opp11` added; `cpu_opp_table0/opp00..opp10` voltage/frequency
  values updated for SM1.
- VPU `compatible` `amlogic, vpu-g12a` → `amlogic, vpu-sm1`.
- HDMI `ic_type = <0x0c>` (SM1), CEC `amlogic, aocec-sm1`, `product_desc = "SM1"`,
  `output = <1>`.
- MMC `compatible` `amlogic, meson-mmc-g12a` → `amlogic, meson-mmc-sm1`.
- Audio bus moved from `@0xff642000` to `@0xFF660000`; `/aml_snd_iomap` moved under
  `/soc` and gained `resampleA/B`, `vad`, `effect`, `earc` nodes.
- Temperature sensors `ff800228/ff634594` → `ff634800/ff634c00`.

### Board / peripherals
- Ethernet: `internal_phy = <1>`, `mc_val = <0x4be04>`, new `eth_reset` register
  (`0xffd01008`), and the `external_eth_pins` pinctrl was removed because
  GPIOZ_14/15 collide with `i2c2` on SM1 (`pinctrl-meson-g12a.c`).
- Amplifier: TAS5707 (`tas5707_36@36`) replaced by AD82584F (`ad82584f_62@62`);
  both are `status = "disabled"` on this board.
- LED controller: node names `is31f3236a@0x78/@0x7e` → `@3c/@3f` (the `reg`
  values were already `0x3c/0x3f`).
- `gpio-keys` replaced by `gpio_keypad` (SM1 polling keypad); key table extended
  with `oemkey` / `dtbo`.
- SD/SDIO restructured into `sd@ffe05000` / `sdio@ffe03000`; SDIO enabled because
  the RTL8822 WiFi module sits on it.
- Frame buffer: `compatible = "amlogic, meson-sm1"`, 1024×600 `custombuilt`.
- Partitions match `partition_yandex_station.dtsi` exactly (18 entries, same
  order, same sizes, `system = 0x5c800000`).

### Deliberate deviations to verify on hardware
- `/efuse` is `status = "disabled"` and `mac` / `deviceid` unifykeys use
  `key-device = "normal"` instead of `"efuse"`. This is consistent with a
  non-secure Linux build, but it means the WiFi MAC is no longer read from eFuse.
- `gpio_keypad` uses polling instead of interrupts — confirm key repeat/latency.
- `root=/dev/mmcblk0p18` matches the stock U-Boot `system-as-root` numbering
  (the on-disk table has extra pre-`logo` partitions), not the DTS part index.

## Build

Self-contained build (no stock image, repository assets only):

```bash
./build_atrios.sh N
```

Firmware only (reuses the already-built rootfs):

```bash
python3 tools/fw_builder/fw_builder.py build --version-num N --verbose
```
