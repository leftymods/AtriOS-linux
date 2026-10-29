# AtriOS Mainline

**AtriOS Mainline** is an upstream port of the Yandex Station Max
(Amlogic S905X3 / SM1) to Linux 7.3 and U-Boot v2026.10, replacing the vendor
Android 9 / kernel 4.9.113 stack.

> **Nothing in this branch has been tested on hardware.**
> Every device is marked `not tested`; `written`, `built` and `verified` only
> mean "compiles and packs on the build host". The first boot will decide what
> actually works.

| Component | Vendor baseline | Mainline target |
|---|---|---|
| Bootloader | U-Boot 2015.01 (Amlogic FIP) | U-Boot v2026.10 |
| Kernel | Linux 4.9.113 (Amlogic BSP) | Linux 7.3 |
| Userspace | Buildroot | Buildroot, reused |

This branch is the port only. `main`, `master` and `slim` keep the 4.9 stack
and are not modified by anything here. Documentation map and per-file status:
[`docs/INDEX.md`](docs/INDEX.md).

## Layout

```
README.md               this file
VERSION                 release number used by the image scripts
docs/                   RE notes, porting plan, INDEX.md
configs/                kernel config fragment (merged over arm64 defconfig)
dts/                    board DTS and its generator (gen-ring-nodes.py)
drivers/                drivers that do not exist upstream yet
patches/linux/          patches against Linux 7.3
patches/uboot/          patches against U-Boot 2026.10 (USB burn gadget)
u-boot/                 defconfig, board/ and DTS for mainline U-Boot
firmware/               firmware blobs the kernel requests (rtw88)
overlay/                userspace overlay applied when an image is built
scripts/                fetch, apply, build and image helpers
tools/                  table converters (rtw88 8822E)
out/                    build artifacts (git-ignored)
```

## Status

All devices are `not tested`; the implementation column only describes code.

| Block | Vendor driver | Mainline plan | Implementation | Hardware |
|---|---|---|---|---|
| LED ring (24 RGB) | `leds-is31fl32xx` + `quasar_led_ring` | upstream driver patch + `ledtrig-ringfb` | written, patches apply to v7.3, built in | **not tested** |
| Front LED matrix 25x16 | `gowin_led_device` + `quasar_led_screen` | SPI fbdev driver, PA/SA backends | written and built in; JTAG/FPGA bitstream not ported | **not tested** |
| Buttons / mic mute | `amlogic,gpio_keypad` | `atrios-keypad` (polled, debounce) | written and built in; checkpatch clean | **not tested** |
| Rotary volume | `rotary-volume` | `rotary-volume` input driver | written, patch applies, built in | **not tested** |
| Ambient light | `ltr308als01` | upstream `ltrf216a` (`liteon,ltr308`) | no driver work needed, built in | **not tested** |
| PCBA ids | `quasar_pcba_ids` | nvmem/at24 + userspace | not implemented | **not tested** |
| Zigbee | `zigbee-control` | new serdev driver | not implemented | **not tested** |
| Audio | AML auge + es7210/es8156/sy6045s | ASoC meson + three ported codecs | codecs are modules; card in DTS (5 dai-links) | **not tested** |
| Bluetooth | `bt-dev` + vendor H5 | `btrtl` + `hci_h5` | DTS/CONFIG ready; vendor firmware project id 33 is rejected by 7.3 `btrtl` | **not tested** |
| Wi-Fi | `88x2es` (RTL8822ES) | rtw88 `rtw8822e` + real 8822E tables | `rtw88_8822es.ko` in `modules.tar`; SDIO host enabled | **not tested** |
| U-Boot | U-Boot 2015.01 (BL33) | U-Boot v2026.10 | `u-boot.bin` 837 232 B; FIP with our BL33 built (BL2/BL30/BL31 vendor) | **not tested** |
| USB burn | vendor `v2_burning` | `aml_burn` gadget on the mainline gadget framework | implemented, compiled into `u-boot.bin` | **not tested** |
| Image | `aml_upgrade_package` | `make-image.sh` / `make-final-image.sh` | test and final images built | **not tested** |

## Quick start

```sh
# sources, patches, kernel and U-Boot (idempotent)
scripts/build-all.sh

# images
scripts/make-image.sh          # intermediate test image (vendor BL33)
scripts/make-fip.sh            # FIP with our BL33
scripts/make-final-image.sh    # final image (no vendor BL33)
```

Artifacts land in `out/` (see [Output](#output)). `build-all.sh` honours
`SRC_DIR` (default `/home/leftymods/mainline-src`). `make-image.sh` and the
final-image scripts additionally need the vendor assets in
`/home/leftymods/atrios-pack` and a Buildroot rootfs tarball; see
`scripts/make-image.sh --help`.

## Build

`scripts/lib.sh` is sourced by every script and resolves all paths from its
own location, so the scripts work from any checkout or worktree.

### Dependencies (Ubuntu 24.04+ / Debian)

```sh
sudo apt-get install -y build-essential gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    bison flex libssl-dev libncurses-dev bc device-tree-compiler python3 \
    xz-utils bzip2 curl git patch kmod rsync
```

`shellcheck` is optional (linting only). The cross compiler is auto-detected
(`aarch64-linux-gnu-gcc` preferred); set `CROSS_COMPILE=/path/to/prefix-`
(trailing dash) to override.

### Commands

```sh
scripts/build-all.sh              # fetch, apply, kernel, u-boot
scripts/build-all.sh --dry-run    # print the plan, build nothing
scripts/build-all.sh --skip-uboot -j 8
scripts/build-all.sh --skip-fetch # reuse the trees in $SRC_DIR

scripts/fetch-sources.sh --dry-run
scripts/apply.sh --kernel-only
scripts/build-kernel.sh -j "$(nproc)" --strict
scripts/build-uboot.sh --dry-run
scripts/make-image.sh --help
```

Every step is idempotent, so re-running after a failure only redoes what is
missing. There is no CI job for this branch; the scripts are checked by hand
(`bash -n` plus `shellcheck` when installed) and by a full run on the
reference host ([Host verification](#host-verification)).

### Scripts

| Script | What it does |
|---|---|
| `scripts/fetch-sources.sh` | Linux 7.3 tarball from kernel.org (size and sha256 pinned, resumable) and U-Boot v2026.10 via `git clone --depth 1`; a tree without `dts/upstream` is rejected |
| `scripts/apply.sh` | board DTS, non-upstream drivers and `patches/linux/*.patch` into the kernel tree; defconfig, board DTS, `board/` and `patches/uboot/*.patch` into the U-Boot tree; already-applied patches are detected and skipped |
| `scripts/build-kernel.sh` | `defconfig` + `configs/atrios.config`, `make Image.gz dtbs modules`, artifacts in `out/linux/` |
| `scripts/build-uboot.sh` | board defconfig + `make`, artifacts in `out/uboot/` |
| `scripts/build-all.sh` | runs fetch, apply, kernel and U-Boot in order; prints artifacts with sha256 |
| `scripts/make-image.sh` | packs the intermediate test image (vendor BL33 + our kernel, DTB and modules) into `out/images/` |
| `scripts/make-fip.sh` | builds the Amlogic FIP with our `u-boot.bin` as BL33 and verifies it |
| `scripts/make-final-image.sh` | replaces the bootloader items with that FIP and packs the final image |

`apply.sh` also covers what upstream Kconfig/Makefiles cannot know:

- patches 0004–0006 add their own Kconfig/Makefile entries;
- codec sources under `drivers/audio/` are copied to `sound/soc/codecs/`
  (patches 0007–0009 add the entries);
- a copied board DTS is added to `arch/arm64/boot/dts/amlogic/Makefile`,
  otherwise `make dtbs` silently never builds it;
- `ledtrig-ringfb.c` gets its wiring from `apply.sh` itself;
- `u-boot/burn/` is copied into the U-Boot tree after the patch, so the
  canonical sources and the applied tree cannot drift;
- a copied driver with no Kconfig/Makefile reference is reported as a warning.

Applied patches are recorded in `<tree>/.atrios-mainline-patches`, which makes
re-runs idempotent for patches that create files. Delete it only after
resetting the tree. A patch that no longer applies aborts the step without
touching the tree; `PATCH_SKIP=0002-*` is the documented escape hatch while a
patch is being refreshed.

### Output

```
out/linux/Image.gz
out/linux/meson-sm1-yandex-station-max.dtb
out/linux/modules.tar
out/uboot/u-boot.bin (+ u-boot.dtb, u-boot-nodtb.bin)
out/fip/u-boot.bin, u-boot.bin.sd.bin, u-boot.bin.usb.*      # make-fip.sh
out/images/atrios-mainline-test-vN.img (+ .sha256)           # make-image.sh
out/images/atrios-mainline-vN.img (+ .sha256)                # make-final-image.sh
```

## Host verification

Reference host, 2026-10-29; sizes and hashes are of the files in `out/`.

- `patches/linux/0002–0011` apply in order to a clean Linux 7.3 tree and are
  recorded in `.atrios-mainline-patches`; a second run detects them.
- `Image.gz` (15 905 990 B), `dtbs` and `modules.tar` (21 773 432 B) build with
  exit 0 under `--strict`.
- `meson-sm1-yandex-station-max.dtb` carries 72 ring channels
  (`station2:{red,green,blue}:ring1..24`), the keypad node (`GPIOA_15/A_14`,
  63/64), rotary `GPIOC_7`/`GPIOAO_10` with codes 104/109, Bluetooth on
  `uart_A` with `enable-gpios` `GPIOX_17`, panel reset `GPIOX_8`, SDIO enabled,
  i2c0 at 400 kHz, and the audio card (`AML-AUGESOUND`, 5 dai-links, four
  codecs on i2c2).
- `fdtget` confirms numeric properties in the packed DTB. (dtc 1.7.2 prints a
  bogus string list for `spi-max-frequency` when decompiling; the blob holds
  `0x3d0900`.)
- `snd-soc-es7210.ko`, `snd-soc-es8156.ko`, `snd-soc-sy6045s.ko`,
  `rtw88_8822es.ko` and the real 8822E tables in `rtw88_8822c.ko` are inside
  `modules.tar`.
- The four board drivers are built in (`ringfb_*`, `atrios_keypad`,
  `rotary_volume_*`, `ledpanel_*` in `System.map`).
- U-Boot v2026.10 builds with `CONFIG_AML_USB_BURN=y`: `u-boot.bin`
  837 232 B, `u-boot.dtb` 75 472 B; the packaged DTB says
  `model = "Yandex Station Max"` with `&usb`/`&dwc2` in peripheral mode.
- The test image (1 571 973 256 B, sha256 `209bc244…da0`) passes
  `ampack verify` and `sha256sum -c`.
- The final image (1 571 493 000 B) passes `ampack verify`, mounts, and its
  `bootloader.PARTITION` is the FIP with our BL33 (1 110 384 B).
- Host checks only: nothing has been booted on the device.

## Final image

| File | Bootloader | Purpose |
|---|---|---|
| `out/images/atrios-mainline-test-vN.img` | vendor U-Boot 2015.01 (BL33) | fallback test image |
| `out/images/atrios-mainline-vN.img` | **our mainline U-Boot** as BL33 inside the FIP | final image |

The final image's sha256 is stored next to it
(`atrios-mainline-vN.img.sha256`); it changes on every rebuild because
`make_ext4fs` stores timestamps. BL2/BL30/BL31 remain the vendor signed
payloads (they contain the board-specific LPDDR4 training); only BL33 is ours.
`scripts/make-fip.sh` verifies the FIP by an LZ4 round-trip and the presence
of `aml_usb_burn_run`; `scripts/make-final-image.sh` packs and verifies it.
See [`docs/final-image.md`](docs/final-image.md) for composition, external
dependencies and flashing risks.

## Environment variables

| Variable | Default | Meaning |
|---|---|---|
| `SRC_DIR` | `/home/leftymods/mainline-src` | tarballs and unpacked trees |
| `OUT_DIR` | `out` | artifacts |
| `KVER` | `7.3` | kernel version |
| `UBOOT_VER` | `2026.10` | U-Boot version |
| `CROSS_COMPILE` | auto | toolchain prefix |
| `JOBS` | `nproc` | parallel jobs |
| `LINUX_SHA256` | pinned in `fetch-sources.sh` | kernel tarball hash (kernel.org) |
| `UBOOT_SHA256` | empty | pin the U-Boot source hash; `--strict` requires it |
| `UBOOT_USE_TARBALL` | `0` | `1` = tarball instead of git clone; the archive must contain `dts/upstream` |
| `PATCH_FUZZ` | `0` | fuzz levels for `patch(1)` when offsets move |
| `PATCH_SKIP` | empty | space-separated patch names/globs to skip (e.g. `0002-*`) |
| `LINUX_DIR` / `UBOOT_DIR` | `$SRC_DIR/...` | override the trees directly |
| `BOARD_DTB` | `meson-sm1-yandex-station-max.dtb` | expected kernel DTB |
| `DEPMOD` | `depmod` (or `true`) | depmod binary used by `modules_install` |
| `BL31` | unset | passed to U-Boot when set |

`make-image.sh` has its own variables (`ATRIOS_PACK`, `VENDOR_STAGING`,
`VENDOR_TOOLS`, `ROOTFS_TAR`, `MODULES_TAR`, `SYSTEM_SIZE`, `VERSION`,
`KERNELRELEASE`, `KERNEL_ADDR`, `ATRIOS_NO_FAKEROOT`); see its `--help`.

## Notes

- The kernel tarball is verified against the kernel.org `sha256sums.asc`
  value (`87363c24…4953`). U-Boot publishes no hash for the GitHub tag, so the
  computed sha256 is printed; pin it in `UBOOT_SHA256` for a reproducible
  build.
- The U-Boot defconfig targets `dts/upstream/src/arm64/amlogic/`, so the tree
  must contain `dts/upstream`. GitHub-generated archives omit it; the denx
  GitLab archive ships it. `fetch-sources.sh` only checks that the directory
  exists, so either source works.
- `configs/atrios.config` lists symbol names that exist in Linux 7.3; comment
  lines record the ones that do not (`GPIO_MESON`, `MMC_SDHCI_MESON`,
  `IIO_LTRF216A`, `MESON_DW_HDMI`, `BT_HCIUART_MESON`).
- The port is labelled **AtriOS Mainline** in `/etc/issue`,
  `/etc/atrios-mainline` and the image marker; all of them state that the
  hardware is not tested.
