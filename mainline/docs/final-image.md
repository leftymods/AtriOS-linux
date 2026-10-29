# Final image (mainline U-Boot + Linux 7.3)

`scripts/make-final-image.sh` builds the image that no longer boots the vendor
U-Boot. It is produced in two steps.

## 1. FIP with our BL33 (`scripts/make-fip.sh`)

An Amlogic image boots a **FIP** (BL2 + BL30 + BL31 + BL33). Only BL33 is
replaceable safely:

| Component | Source | Notes |
|---|---|---|
| BL2 | vendor v112 FIP (or the Khadas VIM3L generic payloads as a fallback) | contains the **board-specific LPDDR4 training** (`board_id`, 1392 MHz). Do not replace it with a generic one on a device you care about. |
| BL30 / BL31 | vendor FIP | ATF/SCP, signed together with BL2. |
| **BL33** | **our mainline U-Boot** (`out/uboot/u-boot.bin`, ~837 KB) | wrapped as `@AML` v3 + LZ4C and re-signed with the non-secure `aml_encrypt_g12a` key. |

`make-fip.sh` verifies the result: FIP size, `@AML` header, LZ4 round-trip
(sha256 of the decompressed payload equals our `u-boot.bin`) and the presence
of `aml_usb_burn_run` in the ELF before packing.

The production FIP is **1.1 MB**, well below the 2 MB burn limit and the 4 MB
`bootloader` partition.

## 2. Package (`scripts/make-final-image.sh`)

The script reuses the staging logic of `make-image.sh` (Android boot image with
`Image.gz` + our DTB as second stage, ext4 system with the 7.3 modules and the
userspace overlay), then replaces only the bootloader items with the new FIP:

```
bootloader.PARTITION / UBOOT.USB / DDR.USB  <- out/uboot FIP
aml_sdc_burn.UBOOT                          <- same payload for SD burning
boot.PARTITION                              <- Image.gz + our DTB
system.PARTITION                            <- rootfs + modules + overlay
```

Packing: `ampack pack --out-ver v2 --verify`, output
`out/images/atrios-mainline-vN.img` plus `.sha256`.

## Build

```sh
mainline/scripts/fetch-sources.sh
mainline/scripts/apply.sh
mainline/scripts/build-kernel.sh --jobs 6 --strict
mainline/scripts/build-uboot.sh -j 6
mainline/scripts/make-fip.sh
mainline/scripts/make-final-image.sh
```

External dependencies of the FIP step (not in git): the vendor v112 FIP
(`/home/leftymods/fip-tmp-v112`), `amlogic-boot-fip` for the generic fallback,
`aml_encrypt_g12a` from the Amlogic FIP tools, and `lz4`. `make-fip.sh` refuses
to build a release FIP without the vendor BL2 payload.

## Flashing

- **USB Burning Tool**: the device must expose `1b8e:c003` (our burn gadget).
  The tool parses the package and sends per-item `download`/`verify` commands;
  `bootloader_is_old` is answered `failed` so it never erases `boot`.
- **SD card**: `aml_sdc_burn.UBOOT` + `aml_sdc_burn.ini` from the same package.

Before the first flash, dump the first 4 MiB of eMMC and keep a copy of the
vendor v112 FIP: it is the rollback path.

## Risks

1. **BL2/DDR** is vendor/generic, not rebuilt from source. If it does not match
   this board revision the device will not reach U-Boot; recovery is the USB
   BootROM mode.
2. **Secure boot**: the FIP is signed with the non-secure key. A device with
   the secure-boot efuse burned will reject it.
3. **No env storage** (`ENV_IS_NOWHERE`): the compiled-in `bootcmd` is the only
   boot policy; `saveenv` does nothing.
4. `boot_a` is 16 MiB and the current boot image is ~15.8 MB — little room for
   a bigger initramfs.
5. The image was not booted on hardware yet; the checks below are the first
   things to run.

## Verification on hardware

```
lsusb -v -d 1b8e:c003                 # burn gadget descriptors
dmesg | grep -i 'usb\|mmc'            # U-Boot side
cat /proc/device-tree/model           # "Yandex Station Max"
cat /proc/cmdline
aplay -l; arecord -l                  # card + PCM 0/1/2
ls /sys/class/graphics/fb*/name       # atrios-ledpanel, led-ring-fb
dmesg | grep -i 'rfe_def\|rtw88'      # Wi-Fi stage (a)/(b)
```
