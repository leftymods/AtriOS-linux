# AtriOS mainline port

Goal: run the Yandex Station Max (Amlogic S905X3 / SM1) on **upstream**
software instead of the vendor Android 9 / kernel 4.9.113 stack.

| Component | Vendor baseline | Mainline target |
|---|---|---|
| Bootloader | U-Boot 2015.01 (Amlogic FIP) | U-Boot v2026.10 |
| Kernel | Linux 4.9.113 (Amlogic BSP) | Linux 7.3 |
| Userspace | Buildroot (see `main` branch) | Buildroot, reused |

Nothing in this directory is wired into the vendor build. `main`, `master`
and `slim` stay on the 4.9 stack; this branch is the upstream port only.

## Layout

```
mainline/
  docs/                 reverse-engineering notes and porting plan
  dts/                  board DTS for the mainline kernel
  drivers/              new drivers that do not exist upstream yet
  patches/              quilt-style patches against Linux 7.3 / U-Boot
  u-boot/               defconfig + DTS for mainline U-Boot
  scripts/              fetch, apply and build helpers
```

## Status

Porting order (agreed): everything else first, Wi-Fi last.

| Block | Vendor driver | Mainline plan | State |
|---|---|---|---|
| LED ring (24 RGB) | `leds-is31fl32xx` + `quasar_led_ring` | upstream `leds-is31fl32xx` patch + `ledtrig-ringfb` | driver written, patches apply to v7.3 |
| Front LED matrix 25x16 | `gowin_led_device` + `quasar_led_screen` | new SPI driver, dual PA/SA backend | driver written, needs hardware test |
| Buttons / mic mute | `amlogic,gpio_keypad` | `atrios-keypad` (polled, debounce) | driver built and checkpatch-clean |
| Rotary volume | `rotary-volume` | `rotary-volume` input driver | driver written, patch applies |
| Ambient light | `ltr308als01` | upstream `ltrf216a` (`liteon,ltr308`) | no driver work needed |
| PCBA ids | `quasar_pcba_ids` | nvmem/at24 + userspace | planned |
| Zigbee | `zigbee-control` | new serdev driver | planned |
| Audio | AML auge + es7210/es8156/sy6045s | ASoC meson + three ported codecs | codecs built; card node still to merge |
| Bluetooth | `bt-dev` + vendor H5 | `btrtl` + `hci_h5` | plan + DTS/CONFIG ready; firmware project-id blocker |
| Wi-Fi | `88x2es` (RTL8822ES) | rtw88 `rtw8822e` stage (a)/(b) | **last**, dossier ready |

## Сборка (build)

Everything is driven by five scripts (`scripts/lib.sh` is shared helpers); the
only state outside this checkout is `$SRC_DIR` (sources) and `mainline/out/`
(artifacts). Nothing is wired into the vendor build.

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
mainline/scripts/build-all.sh            # fetch, apply, kernel, u-boot
mainline/scripts/build-all.sh --dry-run  # print the plan, build nothing
mainline/scripts/build-all.sh --skip-uboot -j 8
```

Every step is idempotent, so re-running after a failure only redoes what is
missing. A full build has not been run in this repository yet (sources are
downloaded on demand); the scripts themselves are linted (`bash -n`,
`shellcheck`) on every change.

### Steps

| Script | What it does |
|---|---|
| `scripts/fetch-sources.sh` | Linux 7.3 tarball from kernel.org (sha256 + size pinned, resumable) and U-Boot v2026.10 via `git clone --depth 1` (the release tarballs omit `dts/upstream`, which `CONFIG_OF_UPSTREAM=y` boards need) |
| `scripts/apply.sh` | board DTS + non-upstream drivers + `patches/linux/*.patch` into the kernel tree; defconfig, board DTS (`dts/upstream/src/arm64/...`) and `board/` into the U-Boot tree; patches already applied are detected and skipped |
| `scripts/build-kernel.sh` | `defconfig` + `configs/atrios.config`, `make Image.gz dtbs modules`, artifacts in `out/linux/` |
| `scripts/build-uboot.sh` | board defconfig + `make`, `out/uboot/u-boot.bin` |
| `scripts/build-all.sh` | runs the four in order and prints artifacts with sha256 |

Useful individual runs:

```sh
mainline/scripts/fetch-sources.sh --dry-run
mainline/scripts/apply.sh --kernel-only
mainline/scripts/build-kernel.sh -j "$(nproc)" --strict
mainline/scripts/build-uboot.sh --dry-run
```

`apply.sh` also covers what upstream Kconfig/Makefiles cannot know about:

- patches 0004-0006 add their own Kconfig/Makefile entries for the input and
  fbdev drivers;
- codec sources under `mainline/drivers/audio/` are copied to
  `sound/soc/codecs/` (their Kconfig entries come from patches 0007-0009);
- a copied board DTS is added to `arch/arm64/boot/dts/amlogic/Makefile`,
  otherwise `make dtbs` silently never builds it;
- a copied driver with no Kconfig/Makefile reference is reported as a warning.

Applied patches are recorded in `<tree>/.atrios-mainline-patches`, which is
what makes re-runs idempotent for patches that create files; delete that file
only after resetting the tree. A patch that no longer applies aborts the step
without touching the tree (`PATCH_SKIP=0002-*` is the documented escape hatch
while a patch is being refreshed).

### Verified on the build host

Checked against a clean Linux 7.3 tree (not just patch dry-runs):

- all nine patches apply in order and are detected on a second run;
- `Image.gz` (15 MB), `dtbs` and `modules` build, exit 0;
- `meson-sm1-yandex-station-max.dtb` carries 72 ring channels, the keypad
  node `GPIOA_15/A_14`, rotary `GPIOC_7`/`GPIOAO_10` with codes 104/109,
  Bluetooth `GPIOX_17`, panel reset `GPIOX_8`, i2c0 at 400 kHz;
- `fdtget` confirms numeric properties in the packed DTB (dtc 1.7.2 prints a
  bogus string list for `spi-max-frequency` when decompiling, the blob itself
  holds `0x3d0900`);
- `snd-soc-es7210.ko`, `snd-soc-es8156.ko`, `snd-soc-sy6045s.ko` link;
- the four board drivers are built into the image (`ringfb_*`,
  `atrios_keypad`, `rotary_volume_*`, `ledpanel_*` in `System.map`).

### Output

```
mainline/out/linux/Image.gz
mainline/out/linux/meson-sm1-yandex-station-max.dtb
mainline/out/linux/modules.tar
mainline/out/uboot/u-boot.bin (+ u-boot.dtb, u-boot-nodtb.bin)
```

The Amlogic FIP (BL2/BL31 packaging) is built separately and is not part of
these scripts.

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
| `UBOOT_USE_TARBALL` | `0` | `1` = tarball instead of git clone (needs `dts/upstream`) |
| `PATCH_FUZZ` | `0` | fuzz levels for `patch(1)` when offsets move |
| `PATCH_SKIP` | empty | space-separated patch names/globs to skip (e.g. `0002-*`) |
| `LINUX_DIR` / `UBOOT_DIR` | `$SRC_DIR/...` | override the trees directly |
| `BOARD_DTB` | `meson-sm1-yandex-station-max.dtb` | expected kernel DTB |
| `DEPMOD` | `depmod` (or `true`) | depmod binary used by `modules_install` |
| `BL31` | unset | passed to U-Boot when set |

### Notes

- The kernel tarball is verified against the kernel.org `sha256sums.asc`
  value (`87363c24…4953`). U-Boot publishes no hash for the GitHub tag, so
  the computed sha256 is printed; pin it in `UBOOT_SHA256` for a fully
  reproducible build.
- The board defconfig targets `dts/upstream/src/arm64/amlogic/`, so the U-Boot
  tree must be a git checkout (or an archive that contains `dts/upstream`).
- `configs/atrios.config` lists the symbol names that actually exist in Linux
  7.3; comment lines record the ones that do not (`GPIO_MESON`,
  `MMC_SDHCI_MESON`, `IIO_LTRF216A`, `MESON_DW_HDMI`, `BT_HCIUART_MESON`,
  `INPUT_ROTARY_VOLUME`).
