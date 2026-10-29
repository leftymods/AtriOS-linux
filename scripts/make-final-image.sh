#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Assemble the FINAL mainline image: mainline U-Boot (BL33) inside the Amlogic
# FIP + mainline Linux 7.3 + the reusable Buildroot userspace.
#
#   scripts/make-final-image.sh [options]
#
# This is the release image, not the intermediate test image:
#
#   bootloader.PARTITION / UBOOT.USB / DDR.USB  -> our FIP (mainline BL33)
#   aml_sdc_burn.UBOOT                          -> the same FIP with the
#                                                  512-byte SD prefix
#   boot.PARTITION, system.PARTITION, dtb       -> rebuilt exactly as in
#                                                  scripts/make-image.sh
#
# The boot/system/dtb work is delegated to make-image.sh so there is a single
# implementation of the Android boot image and the ext4 rootfs.  Only the
# bootloader items are replaced before the final ampack pack.
#
# Environment:
#   OUT_DIR, MAINLINE_DIR   see scripts/lib.sh
#   VERSION                 image version (default <repo>/VERSION)
#   ATRIOS_PACK             vendor assets root (default /home/leftymods/atrios-pack)
#   ATRIOS_NO_FAKEROOT=1    do not re-exec under fakeroot
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

ATRIOS_PACK="${ATRIOS_PACK:-/home/leftymods/atrios-pack}"
VENDOR_STAGING="${VENDOR_STAGING:-$ATRIOS_PACK/tools/fw_builder/staging}"
VENDOR_TOOLS="${VENDOR_TOOLS:-$ATRIOS_PACK/tools/bin}"

IMAGES_DIR="${IMAGES_DIR:-$OUT_DIR/images}"
WORK_DIR="${WORK_DIR:-$IMAGES_DIR/.final-work}"
STAGING_DIR="$WORK_DIR/staging"
FIP_OUT_DIR="${FIP_OUT_DIR:-$OUT_DIR/fip}"
VERSION="${VERSION:-$(tr -d '[:space:]' < "$MAINLINE_DIR/VERSION" 2>/dev/null || echo 0)}"

FIP_FILE=""
BL33_FILE=""
FIP_SRC_ARG=""
NO_LZ4=0
BURN=0
REUSE_STAGING=0
FAKE_ROOT=0

# Options forwarded verbatim to make-image.sh.
IMAGE_ARGS=()

# fakeroot must wrap the whole script (tar, depmod, make_ext4fs) and be set up
# *before* the option loop shifts "$@", otherwise the re-exec would lose the
# command line.  make-image.sh uses the same pattern.
if [ "$(id -u)" != 0 ] && [ "${ATRIOS_NO_FAKEROOT:-0}" != 1 ] &&
	[ -z "${FAKEROOTKEY:-}" ] && command -v fakeroot >/dev/null 2>&1; then
	exec fakeroot -- "$0" --fakeroot "$@"
fi

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Build the final image (mainline U-Boot FIP + mainline Linux + rootfs).

Bootloader options:
      --fip FILE      use an existing FIP instead of building one
      --bl33 FILE     BL33 passed to make-fip.sh (default out/uboot/u-boot.bin)
      --src DIR       amlogic-boot-fip board dir for make-fip.sh
      --no-lz4        do not lz4-compress BL33 (usually > 2 MiB)
      --burn          make-fip.sh burn hook (out/uboot/burn/u-boot.bin)

Image options (forwarded to make-image.sh):
      --rootfs TAR    Buildroot rootfs tarball
      --kernel FILE   compressed kernel (default out/linux/Image.gz)
      --dtb FILE      board DTB (default out/linux/meson-sm1-yandex-station-max.dtb)
      --cmdline FILE  kernel command line
      --kver VER      module directory name
      --kernel-addr A kernel load address in boot.PARTITION
      --no-rootfs     reuse the vendor staging system.PARTITION
      --reuse-staging reuse $STAGING_DIR (skip make-image.sh)
      --fakeroot      re-exec under fakeroot (default: on when not root)
  -h, --help          this help

Environment:
  ATRIOS_PACK=$ATRIOS_PACK
  VENDOR_STAGING=$VENDOR_STAGING
  VENDOR_TOOLS=$VENDOR_TOOLS
  WORK_DIR=$WORK_DIR
  VERSION=$VERSION

Artifacts:
  <out>/atrios-mainline-v$VERSION.img
  <out>/atrios-mainline-v$VERSION.img.sha256
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--fip)
			[ -n "${2:-}" ] || die "--fip requires a file"
			FIP_FILE=$2; shift 2 ;;
		--bl33)
			[ -n "${2:-}" ] || die "--bl33 requires a file"
			BL33_FILE=$2; shift 2 ;;
		--src)
			[ -n "${2:-}" ] || die "--src requires a directory"
			FIP_SRC_ARG=$2; shift 2 ;;
		--no-lz4) NO_LZ4=1; shift ;;
		--burn) BURN=1; shift ;;
		--reuse-staging) REUSE_STAGING=1; shift ;;
		--rootfs|--kernel|--dtb|--cmdline|--kver|--kernel-addr)
			[ -n "${2:-}" ] || die "$1 requires a value"
			IMAGE_ARGS+=("$1" "$2"); shift 2 ;;
		--no-rootfs)
			IMAGE_ARGS+=("--no-rootfs"); shift ;;
		--fakeroot) FAKE_ROOT=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done


check_inputs() {
	[ -x "$VENDOR_TOOLS/ampack" ] || die "ampack not found: $VENDOR_TOOLS/ampack"
	need sha256sum
	need python3
}

build_fip() {
	if [ -n "$FIP_FILE" ]; then
		return 0
	fi

	local args=()
	[ -n "$BL33_FILE" ] && args+=(--bl33 "$BL33_FILE")
	[ -n "$FIP_SRC_ARG" ] && args+=(--src "$FIP_SRC_ARG")
	[ "$NO_LZ4" = 1 ] && args+=(--no-lz4)
	[ "$BURN" = 1 ] && args+=(--burn)

	log "building FIP with mainline U-Boot as BL33"
	FIP_OUT_DIR="$FIP_OUT_DIR" "$MAINLINE_DIR/scripts/make-fip.sh" \
		--out "$FIP_OUT_DIR" "${args[@]}"
	FIP_FILE="$FIP_OUT_DIR/u-boot.bin"
}

build_staging() {
	local boot=$STAGING_DIR/boot.PARTITION
	local system=$STAGING_DIR/system.PARTITION
	local dtb=$STAGING_DIR/_aml_dtb.PARTITION

	if [ "$REUSE_STAGING" = 1 ]; then
		[ -f "$boot" ] && [ -f "$system" ] && [ -f "$dtb" ] ||
			die "--reuse-staging: $STAGING_DIR is incomplete (need boot/system/dtb)"
		log "--reuse-staging: reusing $STAGING_DIR"
		return 0
	fi

	log "building boot/system/dtb with scripts/make-image.sh"
	# make-image.sh packs a throw-away test image; only its staging is used.
	FINAL_IMAGE=1 "$MAINLINE_DIR/scripts/make-image.sh" \
		--out "$WORK_DIR/images" --staging "$STAGING_DIR" "${IMAGE_ARGS[@]}"

	[ -f "$boot" ] || die "make-image.sh did not produce $boot"
	[ -f "$system" ] || die "make-image.sh did not produce $system"
	[ -f "$dtb" ] || die "make-image.sh did not produce $dtb"
	# The throw-away test image is ~1.5 GiB; drop it now.
	rm -f "$WORK_DIR/images"/atrios-mainline-test-*.img \
		"$WORK_DIR/images"/atrios-mainline-test-*.img.sha256
}

install_bootloader() {
	local fip=$1 sd="${1}.sd.bin"

	[ -f "$fip" ] || die "FIP not found: $fip"
	[ -f "$sd" ] || die "FIP SD variant not found: $sd"
	[ -f "$VENDOR_STAGING/aml_sdc_burn.ini" ] ||
		die "vendor asset missing: $VENDOR_STAGING/aml_sdc_burn.ini"
	[ -f "$VENDOR_STAGING/platform.conf" ] ||
		die "vendor asset missing: $VENDOR_STAGING/platform.conf"
	[ -f "$VENDOR_STAGING/logo.PARTITION" ] ||
		die "vendor asset missing: $VENDOR_STAGING/logo.PARTITION"

	log "installing the FIP into the bootloader items"
	# make-image.sh hard-links the vendor assets, so the destination may be
	# the same inode as the file in atrios-pack.  Remove it first: cp -f over
	# a hard link would rewrite the vendor staging in place.
	local f
	for f in bootloader.PARTITION UBOOT.USB DDR.USB aml_sdc_burn.UBOOT; do
		rm -f "$STAGING_DIR/$f"
	done
	cp -f "$fip" "$STAGING_DIR/bootloader.PARTITION"
	cp -f "$fip" "$STAGING_DIR/UBOOT.USB"
	cp -f "$fip" "$STAGING_DIR/DDR.USB"
	cp -f "$sd" "$STAGING_DIR/aml_sdc_burn.UBOOT"
	# These three are not rebuilt; make-image.sh already staged them.
	for f in aml_sdc_burn.ini platform.conf logo.PARTITION; do
		[ -f "$STAGING_DIR/$f" ] || cp -f "$VENDOR_STAGING/$f" "$STAGING_DIR/$f"
	done
}

verify_bootloader() {
	local fip=$STAGING_DIR/bootloader.PARTITION
	local bl33=${BL33_FILE:-$OUT_DIR/uboot/u-boot.bin}

	[ -f "$bl33" ] || die "BL33 not found for verification: $bl33"
	log "verifying the FIP in bootloader.PARTITION"
	FIP_OUT_DIR="$FIP_OUT_DIR" "$MAINLINE_DIR/scripts/make-fip.sh" \
		--verify "$fip" --bl33 "$bl33"
}

print_summary() {
	local img=$1 f
	echo
	log "final image: $img"
	printf '    %-12s %s\n' "size" "$(stat -c %s "$img") B"
	printf '    %-12s %s\n' "sha256" "$(sha256_of "$img")"
	echo
	log "bootloader items"
	for f in bootloader.PARTITION UBOOT.USB DDR.USB aml_sdc_burn.UBOOT; do
		printf '    %12s  %s\n' "$(stat -c %s "$STAGING_DIR/$f")" "$f"
	done
	echo
	log "packed items"
	for f in "$STAGING_DIR"/*; do
		[ -f "$f" ] || continue
		printf '    %12s  %s\n' "$(stat -c %s "$f")" "$(basename "$f")"
	done
}

main() {
	check_inputs
	mkdir -p "$IMAGES_DIR" "$WORK_DIR"

	build_fip
	build_staging
	install_bootloader "$FIP_FILE"

	# Verify the FIP after it was copied, so we check the exact bytes that
	# go into the image (and not just the build directory).
	verify_bootloader

	local img="$IMAGES_DIR/atrios-mainline-v$VERSION.img"
	log "packing with ampack (v2, verify)"
	"$VENDOR_TOOLS/ampack" pack --out-ver v2 --verify "$STAGING_DIR" "$img"

	log "ampack verify $img"
	"$VENDOR_TOOLS/ampack" verify "$img" >/dev/null

	( cd "$IMAGES_DIR" && sha256sum "$(basename "$img")" ) > "$img.sha256"

	print_summary "$img"
	echo
	warn "FINAL image: mainline U-Boot (BL33) inside the Amlogic FIP."
	warn "BL2/BL30/BL31 come from the vendor v112 signed intermediates when"
	warn "FIP_STAGE_DIR is available; only BL33 is new. See docs/final-image.md."
}

main "$@"
