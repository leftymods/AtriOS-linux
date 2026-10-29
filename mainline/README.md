# AtriOS Mainline

**AtriOS Mainline** — upstream port of the Yandex Station Max to Linux 7.3 and
U-Boot v2026.10.

Goal: run the Yandex Station Max (Amlogic S905X3 / SM1) on **upstream**
software instead of the vendor Android 9 / kernel 4.9.113 stack.

| Component | Vendor baseline | Mainline target |
|---|---|---|
| Bootloader | U-Boot 2015.01 (Amlogic FIP) | U-Boot v2026.10 |
| Kernel | Linux 4.9.113 (Amlogic BSP) | Linux 7.3 |
| Userspace | Buildroot (see `main` branch) | Buildroot, reused |

Nothing in this directory is wired into the vendor build. `main`, `master`
and `slim` stay on the 4.9 stack; this branch is the upstream port only.

Documentation map, per-file status and the list of known documentation
mismatches: **`docs/INDEX.md`**.

## Layout

```
mainline/
  docs/                 reverse-engineering notes, porting plan, INDEX.md
  configs/              kernel config fragment (merged over arm64 defconfig)
  dts/                  board DTS and its generator (gen-ring-nodes.py)
  drivers/              new drivers that do not exist upstream yet
  patches/linux/        patches against Linux 7.3
  patches/uboot/        patches against U-Boot 2026.10 (USB burn gadget)
  u-boot/               defconfig, board/ and DTS for mainline U-Boot
  firmware/             firmware blobs the kernel requests (rtw88)
  overlay/              userspace overlay used when an image is built
  scripts/              fetch, apply, build and image helpers
  out/                  build artifacts (git-ignored)
```

## Status

> **Nothing in this branch has been tested on hardware.**
> Every device below is marked `not tested` regardless of how complete its
> code is: "written", "built" and "verified" only mean "compiles on the build
> host". The first boot will decide what actually works.

| Block | Vendor driver | Mainline plan | Implementation | Hardware |
|---|---|---|---|---|
| LED ring (24 RGB) | `leds-is31fl32xx` + `quasar_led_ring` | upstream `leds-is31fl32xx` patch + `ledtrig-ringfb` | driver written, patches apply to v7.3, built into `Image.gz` | **not tested** |
| Front LED matrix 25x16 | `gowin_led_device` + `quasar_led_screen` | new SPI fbdev driver, dual PA/SA backend | driver written and built in; JTAG/FPGA bitstream not ported | **not tested** |
| Buttons / mic mute | `amlogic,gpio_keypad` | `atrios-keypad` (polled, debounce) | driver written and built in; checkpatch clean | **not tested** |
| Rotary volume | `rotary-volume` | `rotary-volume` input driver | driver written, patch applies, built in | **not tested** |
| Ambient light | `ltr308als01` | upstream `ltrf216a` (`liteon,ltr308`) | no driver work needed, driver built in | **not tested** |
| PCBA ids | `quasar_pcba_ids` | nvmem/at24 + userspace | not implemented | **not tested** |
| Zigbee | `zigbee-control` | new serdev driver | not implemented | **not tested** |
| Audio | AML auge + es7210/es8156/sy6045s | ASoC meson + three ported codecs | codecs built as modules, card in DTS (5 dai-links) | **not tested** |
| Bluetooth | `bt-dev` + vendor H5 | `btrtl` + `hci_h5` | DTS/CONFIG ready; vendor firmware project id 33 is rejected by 7.3 `btrtl` | **not tested** |
| Wi-Fi | `88x2es` (RTL8822ES) | rtw88 `rtw8822e` + real 8822E tables | `rtw88_8822es.ko` in `modules.tar`, SDIO host enabled | **not tested** |
| U-Boot | U-Boot 2015.01 (BL33) | U-Boot v2026.10 | `u-boot.bin` 837 232 B; FIP with our BL33 built (BL2/BL30/BL31 vendor) | **not tested** |
| USB burn | vendor `v2_burning` | `aml_burn` gadget on the mainline gadget framework | implemented, compiled into `u-boot.bin` | **not tested** |
| Image | `aml_upgrade_package` | `make-image.sh` / `make-final-image.sh` | test image (vendor BL33) and final image (our BL33) built | **not tested** |

## Quick start

```sh
# 1. sources + patches + kernel + U-Boot (idempotent)
mainline/scripts/build-all.sh

# 2. optional: the intermediate test image (vendor BL33 + our kernel)
mainline/scripts/make-image.sh
```

Artifacts land in `mainline/out/` (see [Output](#output)). `build-all.sh`
honours `SRC_DIR` (default `/home/leftymods/mainline-src`); `make-image.sh`
additionally needs the vendor assets in `/home/leftymods/atrios-pack` and a
Buildroot rootfs tarball, see its `--help`.

## Сборка (build)

Everything is driven by these scripts (`scripts/lib.sh` is shared helpers):
`fetch-sources.sh`, `apply.sh`, `build-kernel.sh`, `build-uboot.sh`,
`build-all.sh` and `make-image.sh`. The state outside this checkout is
`$SRC_DIR` (sources and unpacked trees), `mainline/out/` (artifacts) and - for
`make-image.sh` only - `atrios-pack` plus the rootfs tarball. Nothing is wired
into the vendor build.

### Dependencies (Ubuntu 24.04+ / Debian)

```sh
sudo apt-get install -y build-essential gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    bison flex libssl-dev libncurses-dev bc device-tree-compiler python3 \
    xz-utils bzip2 curl git patch kmod rsync
```

`shellcheck` is optional and only used by `scripts/*.sh` linting.

A cross compiler is auto-detected (`aarch64-linux-gnu-gcc` is preferred); set
`CROSS_COMPILE=/path/to/prefix-` (trailing dash) to override. Building the
kernel or U-Boot with a host-only compiler is rejected.

### One command

```sh
mainline/scripts/build-all.sh              # fetch, apply, kernel, u-boot
mainline/scripts/build-all.sh --dry-run    # print the plan, build nothing
mainline/scripts/build-all.sh --skip-uboot -j 8
mainline/scripts/build-all.sh --skip-fetch # reuse the trees in $SRC_DIR
```

Every step is idempotent, so re-running after a failure only redoes what is
missing. All steps have been run on the reference host (see
[Verified on the build host](#verified-on-the-build-host)); there is no CI job
for `mainline/`, so the scripts are only checked by hand (`bash -n`, and
`shellcheck` when it is installed).

### Steps

| Script | What it does |
|---|---|
| `scripts/fetch-sources.sh` | Linux 7.3 tarball from kernel.org (sha256 + size pinned, resumable) and U-Boot v2026.10 via `git clone --depth 1` (a tree without `dts/upstream`, which `CONFIG_OF_UPSTREAM=y` needs, is rejected) |
| `scripts/apply.sh` | board DTS + non-upstream drivers + `patches/linux/*.patch` into the kernel tree; defconfig, board DTS (`dts/upstream/src/arm64/...`), `board/` and `patches/uboot/*.patch` into the U-Boot tree; patches already applied are detected and skipped |
| `scripts/build-kernel.sh` | `defconfig` + `configs/atrios.config`, `make Image.gz dtbs modules`, artifacts in `out/linux/` |
| `scripts/build-uboot.sh` | board defconfig + `make`, artifacts in `out/uboot/` |
| `scripts/build-all.sh` | runs the four in order and prints artifacts with sha256 |
| `scripts/make-image.sh` | packs the intermediate test image (vendor BL33 + our `Image.gz`, DTB and modules) into `out/images/` |

Useful individual runs:

```sh
mainline/scripts/fetch-sources.sh --dry-run
mainline/scripts/apply.sh --kernel-only
mainline/scripts/build-kernel.sh -j "$(nproc)" --strict
mainline/scripts/build-uboot.sh --dry-run
mainline/scripts/make-image.sh --help
```

`apply.sh` also covers what upstream Kconfig/Makefiles cannot know about:

- patches 0004-0006 add their own Kconfig/Makefile entries for the input and
  fbdev drivers;
- codec sources under `mainline/drivers/audio/` are copied to
  `sound/soc/codecs/` (their Kconfig entries come from patches 0007-0009);
- a copied board DTS is added to `arch/arm64/boot/dts/amlogic/Makefile`,
  otherwise `make dtbs` silently never builds it;
- `ledtrig-ringfb.c` gets its Kconfig/Makefile wiring from `apply.sh` itself;
- a copied driver with no Kconfig/Makefile reference is reported as a warning.

Applied patches are recorded in `<tree>/.atrios-mainline-patches`, which is
what makes re-runs idempotent for patches that create files; delete that file
only after resetting the tree. A patch that no longer applies aborts the step
without touching the tree (`PATCH_SKIP=0002-*` is the documented escape hatch
while a patch is being refreshed).

### Verified on the build host

State of the reference host, 2026-10-29 (artifacts in `mainline/out/`, sizes
and hashes are of those files):

- `patches/linux/0002-0009` apply in order to a clean Linux 7.3 tree and are
  recorded in `.atrios-mainline-patches` (so a second run detects them);
  `0010-rtw88-8822e.patch` was added afterwards and is recorded separately in
  `docs/wifi-port.md`;
- `Image.gz` (15 905 990 B), `dtbs` and `modules.tar` (21 773 432 B) build,
  exit 0;
- `meson-sm1-yandex-station-max.dtb` carries 72 ring channels
  (`station2:{red,green,blue}:ring1..24`), the keypad node `GPIOA_15/A_14`
  (63/64), rotary `GPIOC_7`/`GPIOAO_10` with codes 104/109, Bluetooth on
  `uart_A` with `enable-gpios` `GPIOX_17`, panel reset `GPIOX_8`, i2c0 at
  400 kHz, and the audio card (`AML-AUGESOUND`, 5 dai-links, four codecs on
  i2c2);
- `fdtget` confirms numeric properties in the packed DTB (dtc 1.7.2 prints a
  bogus string list for `spi-max-frequency` when decompiling, the blob itself
  holds `0x3d0900`);
- `snd-soc-es7210.ko`, `snd-soc-es8156.ko`, `snd-soc-sy6045s.ko` link and are
  inside `modules.tar`, including `rtw88_8822es.ko` and the real 8822E
  tables in `rtw88_8822c.ko` (see `docs/wifi-port.md`);
- the four board drivers are built into the image (`ringfb_*`,
  `atrios_keypad`, `rotary_volume_*`, `ledpanel_*` in `System.map`);
- U-Boot v2026.10 builds with `CONFIG_AML_USB_BURN=y`:
  `u-boot.bin` 837 232 B, `u-boot.dtb` 75 472 B, `u-boot-nodtb.bin`
  752 912 B; the packaged DTB says `model = "Yandex Station Max"` and has
  `&usb`/`&dwc2` enabled (peripheral);
- the test image `out/images/atrios-mainline-test-v130.img`
  (1 571 973 256 B, sha256 `209bc244…da0`) passes `ampack verify` and
  `sha256sum -c`; host checks only, it has never been booted on the device
  (`docs/image.md`).

### Output

```
mainline/out/linux/Image.gz
mainline/out/linux/meson-sm1-yandex-station-max.dtb
mainline/out/linux/modules.tar
mainline/out/uboot/u-boot.bin (+ u-boot.dtb, u-boot-nodtb.bin)
mainline/out/images/atrios-mainline-test-v130.img (+ .sha256)   # make-image.sh
```

The Amlogic FIP (BL2/BL31 packaging) is built separately and is not part of
these scripts.

### Final image

Two images exist:

| File | Bootloader | Purpose |
|---|---|---|
| `out/images/atrios-mainline-test-v130.img` | vendor U-Boot 2015.01 (BL33) | fallback test image |
| `out/images/atrios-mainline-v130.img` | **our mainline U-Boot** as BL33 inside the FIP | final image |

The final image is 1 571 493 000 B; its sha256 is in
`atrios-mainline-v130.img.sha256` next to the file (the hash changes with every
rebuild because make_ext4fs stores timestamps).
BL2/BL30/BL31 remain the vendor signed payloads (they contain the
board-specific LPDDR4 training); only BL33 is ours. The FIP is built by
`scripts/make-fip.sh`, verified by an LZ4 round-trip and the presence of
`aml_usb_burn_run`, and packed by `scripts/make-final-image.sh`. See
`docs/final-image.md` for the composition, external dependencies and flashing
risks. Nothing has been booted on hardware yet.

### Environment variables

| Variable | Default | Meaning |
|---|---|---|
| `SRC_DIR` | `/home/leftymods/mainline-src` | tarballs and unpacked trees |
| `OUT_DIR` | `mainline/out` | artifacts |
| `KVER` | `7.3` | kernel version |
| `UBOOT_VER` | `2026.10` | U-Boot version |
| `CROSS_COMPILE` | auto | toolchain prefix |
| `JOBS` | `nproc` | parallel jobs |
| `LINUX_SHA256` | pinned in `fetch-sources.sh` | kernel tarball hash (kernel.org) |
| `UBOOT_SHA256` | empty | pin the U-Boot source hash; `--strict` requires it |
| `UBOOT_USE_TARBALL` | `0` | `1` = tarball instead of git clone (the archive must contain `dts/upstream`) |
| `PATCH_FUZZ` | `0` | fuzz levels for `patch(1)` when offsets move |
| `PATCH_SKIP` | empty | space-separated patch names/globs to skip (e.g. `0002-*`) |
| `LINUX_DIR` / `UBOOT_DIR` | `$SRC_DIR/...` | override the trees directly |
| `BOARD_DTB` | `meson-sm1-yandex-station-max.dtb` | expected kernel DTB |
| `DEPMOD` | `depmod` (or `true`) | depmod binary used by `modules_install` |
| `BL31` | unset | passed to U-Boot when set |

`make-image.sh` has its own variables (`ATRIOS_PACK`, `VENDOR_STAGING`,
`VENDOR_TOOLS`, `ROOTFS_TAR`, `MODULES_TAR`, `SYSTEM_SIZE`, `VERSION`,
`KERNELRELEASE`, `KERNEL_ADDR`, `ATRIOS_NO_FAKEROOT`); see its `--help`.

### Notes

- The kernel tarball is verified against the kernel.org `sha256sums.asc`
  value (`87363c24…4953`). U-Boot publishes no hash for the GitHub tag, so
  the computed sha256 is printed; pin it in `UBOOT_SHA256` for a fully
  reproducible build.
- The board defconfig targets `dts/upstream/src/arm64/amlogic/`, so the U-Boot
  tree must contain `dts/upstream` (git checkout, or an archive that ships
  it). The v2026.10 tag archive does ship it (checked 2026-10-29), although
  `fetch-sources.sh` still states the opposite; the script only *checks* for
  the directory, so either source works.
- `configs/atrios.config` lists the symbol names that actually exist in Linux
  7.3; comment lines record the ones that do not (`GPIO_MESON`,
  `MMC_SDHCI_MESON`, `IIO_LTRF216A`, `MESON_DW_HDMI`, `BT_HCIUART_MESON`,
  `INPUT_ROTARY_VOLUME`). Two of its comments are stale (`lite-on,ltr308als01`,
  `gpio-keys`) - listed in `docs/INDEX.md` §2 for the owner.
