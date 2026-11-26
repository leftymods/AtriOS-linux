# Поддерживаемые устройства MainlineAtriOS (`config/boards/`)

**AtriStation** — софт-модификация Яндекс Станции Макс **YNDX-0053 MAX**. Целевая аппаратная версия использует **Amlogic S905X3 (`sm1`) вместо S905X2**. Профиль `station-max`, Device Tree и образы в этом репозитории рассчитаны на вариант с S905X3.

| Board ID | Платформа | Семейство / SoC | Конфиг сборки | Статус |
|---|---|---|---|---|
| `station-max` (`yndxsttion53`) | **AtriStation Max** (YNDX-0053 MAX) | `amlogic-meson64` / Amlogic S905X3 (`sm1`), вместо S905X2 | `config/boards/station-max.conf` (`yndxsttion53.conf`) | Полная поддержка |
| `template` | Шаблон для новой колонки (`new-board`) | Настраивается (`amlogic-meson64` и др.) | `config/boards/template.conf` | Шаблон расширения |

---

## Добавление новой колонки во фреймворк

1. Скопируйте `config/boards/template.conf` в `config/boards/<board>.conf` и задайте параметры `BOARD`, `VENDOR`, `BOARDFAMILY`, `SOC`, `DTS_NAME`, `UBOOT_DEFCONFIG`, `KERNEL_DEFCONFIG`, `ROOTFS_OVERLAY`.
2. Разместите Device Tree, defconfig и overlay в `board/<vendor>/<device>/`.
3. Добавьте профиль платы (по `compatible` из Device Tree) в `/usr/lib/atrios/device-detect` (для `fastfetch`, `S00resizefs` и проверки совместимости при OTA).
4. Add a reviewed boot recipe at `config/bootloaders/<DTS_NAME>.conf` (or select its name through `BOOTLOADER_PROFILE`). The recipe must set repository-relative `UBOOT_CONFIG_SOURCE`, `UBOOT_DTS_SOURCE`, tree-relative `UBOOT_DTS_TARGET`, and an explicit board-compatible `FIP_PROFILE`. Optional `UBOOT_DTS_OVERLAY_SOURCE`/`UBOOT_DTS_OVERLAY_TARGET`, `UBOOT_AML_BURN_SOURCE`, and `UBOOT_REQUIRED_OPTIONS` belong to that recipe. Use the existing Station recipe as a schema example, not as firmware for another board. `UPGRADE_DIR` selects this board's output directory; a missing recipe fails before compilation instead of using Station firmware.
5. Register hardware approved for publication in both the release workflow matrix and its publish allowlist. CI resolves the trust anchor, DTB and image filename from that board's configuration. Adding a config or passing synthetic build tests does not establish physical hardware support.


Статус поддерживаемых драйверов ядра по устройствам см. в [DRIVERS.md](DRIVERS.md).

## Board console

Set `SERIAL_CONSOLE` in the board profile, for example `ttyAML0,115200`,
`ttyS0,9600n8`, `ttyAMA0,115200n8`, or `hvc0`. Family settings supply a
console only when the board has not selected one. The same parameter supplies
the kernel command line, `/etc/atrios-console`, `securetty` and the systemd
serial-getty instance. The custom init reads this data through the common
console helper and opens the configured character device; if it is unavailable,
it uses `/dev/console`. Console data is validated and never executed as shell
code. Login TTY permissions remain restricted to root.

## Additional board runtime files for OTA

An optional `OTA_RUNTIME_MANIFEST` board field can select a reviewed,
project-relative JSON file, for example
`{"schema":1,"paths":["usr/lib/vendor/helper"]}`. These files or directories
are added to the common HTTPS runtime payload from the built rootfs, not from
unprocessed source overlays. The staging helper rejects escaping paths,
symlinks, credentials, mutable user state, `root`/`var`, the retired API server
and AEC binaries. Register only code and public configuration that an update
may replace; models, private keys and user settings remain outside the bundle.


## Isolated kernel profiles

Kernel builds use `cache/kernel-profiles/<BOARD>/<fingerprint>` instead of a shared
mutable `cache/kernel`. The fingerprint covers the board ID, archive URL and
SHA-256 (or the explicit local source contents), defconfig, DTS, repository driver
sources, ordered patch contents, architecture, DTS vendor, image target and cross
compiler prefix. Changing any of these inputs selects a different tree; another
board's patches and configuration are never reused implicitly. Existing legacy
caches remain untouched. An explicit `KERNEL_SOURCE_DIR` is copied into a private
profile, with internal absolute symlinks rewritten and the known repository driver
link materialized; it is not patched or built in place. Git metadata and prior
AtriOS source-backup directories are omitted from this build snapshot.

`KERNEL_ARCH` selects the Linux make architecture (`arm64` or `arm`), independently
of Debian `ARCH` (`arm64`, `armhf` or `armel`). `KERNEL_DTS_SUBDIR` selects the
vendor path under `arch/$KERNEL_ARCH/boot/dts` (an explicitly empty value selects
that directory itself). `KERNEL_DTB_CONFIG` selects the DTB make condition, such as
`CONFIG_ARCH_MESON` or `y`. `KERNEL_IMAGE_TARGET` selects `Image`, `zImage`, or another
reviewed boot image make target. Only the Meson family defaults to `amlogic` and
`CONFIG_ARCH_MESON`; other families must declare their DTS location. These fields
configure build routing and do not claim physical validation for a new board.

Optional `KERNEL_CONFIG_PATH`, `KERNEL_DTS_PATH`, `KERNEL_PATCH_DIR` and
`KERNEL_DRIVERS_DIR` override project-relative input paths. The existing defconfig,
DTS and patch paths under `UBOOT_SOURCE_DIR` remain defaults. Consumers should
source `lib/compilation-kernel.sh` after loading the board and call
`resolve_kernel_build_profile`; it exports `KERNEL_BUILD_DIR` and resolved
`KERNEL_BUILD_*` metadata without fetching or modifying sources. Passing `--prepare`
creates that selected profile. Packaging takes its kernel, DTB and modules from
that tree, rather than trusting a shared output file. Verification can inspect
routing without building: `scripts/verify_all.sh --board <BOARD> --profile-only`.

## Pinned bootloader sources

Each reviewed boot recipe must declare `UBOOT_GIT_URL`, `UBOOT_GIT_COMMIT`,
`FIP_GIT_URL` and `FIP_GIT_COMMIT`. Commits are complete 40-character lowercase
Git SHA-1 IDs; branch names, tags and abbreviated hashes do not satisfy a pin.
New sources fetch the exact commit into a separate cache. An explicitly selected
existing cache must have that HEAD; mismatches fail without resetting or cleaning
local work. Matching legacy caches remain reusable, and different pins select
separate default cache directories. `UBOOT_SRC` and `FIP_TOOL_DIR` can select
existing local worktrees explicitly.

`SOURCE_DATE_EPOCH` defaults to the pinned U-Boot commit time. After a successful
build, `UPGRADE_DIR/bootloader-sources.json` records both source commits and trees,
local tracked-diff and untracked-file fingerprints, config/DTS hashes and final
bootloader artifact hashes. Locally modified trees are preserved and marked as
dirty in this record; their fingerprints do not replace committing/reviewing
those modifications when a reproducible custom base is required.

The basename of `KERNEL_DTS_PATH` must equal `DTS_NAME + ".dts"`; mismatched
logical and source names are rejected before source preparation. Both `Kbuild`
and `Makefile` participate in custom driver staging and cache identity.

### Root filesystem emulator and kernel boot command

Debian `ARCH=arm64` selects `qemu-aarch64-static`; `armhf` and `armel` select `qemu-arm-static`. A reviewed `ROOTFS_QEMU_BINARY` executable may be supplied as an absolute or project-relative path for another architecture or emulator build. Its basename and SHA-256 enter the base-cache profile; the selected binary is copied under that same basename for chroot execution and removed before packaging. The ARM64 loader repair is applied only to ARM64 roots.

`UBOOT_KERNEL_BOOT_COMMAND` accepts only `booti`, `bootz` or `bootm`. The default is `booti` for an ARM64 kernel and `bootz` for an ARM kernel. Other kernel architectures require an explicit command reviewed against the selected image format and U-Boot configuration. The boot partition still uses the common `/Image` payload filename, independent of the kernel build target.
