#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build mainline U-Boot for the Yandex Station Max.
#
#   scripts/build-uboot.sh [--jobs N] [--dry-run] [--help]
#
# The board defconfig comes from the U-Boot port; it is integrated into the
# tree by scripts/apply.sh.  Both layouts are accepted:
#   mainline/u-boot/yandex-station-max_defconfig
#   mainline/u-boot/configs/yandex-station-max_defconfig
# plus any other single *_defconfig found there.
#
# Environment:
#   SRC_DIR, UBOOT_VER     see scripts/lib.sh
#   UBOOT_DIR              U-Boot tree        (default $SRC_DIR/u-boot-$UBOOT_VER)
#   UBOOT_DEFCONFIG        defconfig name/path override
#   OUT_DIR                artifact root      (default mainline/out)
#   CROSS_COMPILE          toolchain prefix   (auto-detected aarch64-*-gcc)
#   BL31                   optional BL31 binary passed to U-Boot
#   JOBS                   parallel jobs      (default nproc)
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

JOBS="${JOBS:-$(nproc)}"
BUILD_DIR="${BUILD_DIR:-$OUT_DIR/uboot/build}"
ART_DIR="$OUT_DIR/uboot"
UBOOT_DEFCONFIG="${UBOOT_DEFCONFIG:-}"

DRY_RUN=0

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Build U-Boot $UBOOT_VER for the Yandex Station Max (arm64/Amlogic SM1).

Options:
  -j, --jobs N   parallel build jobs (default $JOBS)
      --dry-run  print the commands that would run, build nothing
  -h, --help     this help

Environment:
  UBOOT_DIR=$UBOOT_DIR
  BUILD_DIR=$BUILD_DIR
  UBOOT_DEFCONFIG=${UBOOT_DEFCONFIG:-<auto>}
  CROSS_COMPILE=${CROSS_COMPILE:-<auto>}
  BL31=${BL31:-<not set>}

Artifacts:
  $ART_DIR/u-boot.bin (plus u-boot.dtb / u-boot-nodtb.bin when built)
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		-j|--jobs)
			[ -n "${2:-}" ] || die "--jobs requires a number"
			JOBS=$2; shift 2 ;;
		-j[0-9]*)
			JOBS=${1#-j}; shift ;;
		--dry-run) DRY_RUN=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done

resolve_defconfig() {
	local c
	shopt -s nullglob
	local known=(
		"$MAINLINE_DIR"/u-boot/yandex-station-max_defconfig
		"$MAINLINE_DIR"/u-boot/yandex_station_max_defconfig
		"$MAINLINE_DIR"/u-boot/configs/yandex-station-max_defconfig
	)
	local any=("$MAINLINE_DIR"/u-boot/*_defconfig "$MAINLINE_DIR"/u-boot/configs/*_defconfig)
	shopt -u nullglob

	for c in "${known[@]}"; do
		[ -f "$c" ] && { UBOOT_DEFCONFIG=$(basename "$c"); return 0; }
	done
	if [ ${#any[@]} -eq 1 ]; then
		UBOOT_DEFCONFIG=$(basename "${any[0]}")
		warn "using the only defconfig found: ${any[0]#"$MAINLINE_DIR"/}"
		return 0
	elif [ ${#any[@]} -gt 1 ]; then
		die "several U-Boot defconfigs found, pick one with UBOOT_DEFCONFIG=:
$(printf '    %s\n' "${any[@]##*/}")"
	fi

	die "no U-Boot defconfig in mainline/u-boot/
    Expected one of:
      mainline/u-boot/yandex-station-max_defconfig
      mainline/u-boot/configs/yandex-station-max_defconfig
    or set UBOOT_DEFCONFIG=<name>_defconfig."
}

ubuild() {
	if [ "$DRY_RUN" = 1 ]; then
		printf '    make -C %s O=%s ARCH=arm CROSS_COMPILE=%s %s\n' \
			"$UBOOT_DIR" "$BUILD_DIR" "$CROSS_COMPILE" "$*"
		return 0
	fi
	local -a extra=()
	[ -n "${BL31:-}" ] && extra+=("BL31=$BL31")
	make -C "$UBOOT_DIR" O="$BUILD_DIR" ARCH=arm \
		CROSS_COMPILE="$CROSS_COMPILE" "${extra[@]}" "$@"
}

main() {
	[ -f "$UBOOT_DIR/Makefile" ] || die "U-Boot tree not found: $UBOOT_DIR
    Run scripts/fetch-sources.sh first (or set UBOOT_DIR)."

	[ -n "$UBOOT_DEFCONFIG" ] || resolve_defconfig
	log "u-boot:  $UBOOT_DIR"
	log "build:   $BUILD_DIR"
	log "config:  $UBOOT_DEFCONFIG"

	[ -f "$UBOOT_DIR/configs/$UBOOT_DEFCONFIG" ] ||
		die "configs/$UBOOT_DEFCONFIG is not in the U-Boot tree.
    Run scripts/apply.sh to copy the board files, or set UBOOT_DEFCONFIG."

	local board_dts dt_base
	board_dts=$(sed -n 's/^CONFIG_DEFAULT_DEVICE_TREE="\(.*\)"/\1/p' \
		"$UBOOT_DIR/configs/$UBOOT_DEFCONFIG" | head -n1)
	dt_base="$UBOOT_DIR/arch/arm/dts"
	if grep -q '^CONFIG_OF_UPSTREAM=y' "$UBOOT_DIR/configs/$UBOOT_DEFCONFIG"; then
		dt_base="$UBOOT_DIR/dts/upstream/src/arm64"
		[ -d "$dt_base" ] ||
			die "$UBOOT_DIR has no dts/upstream/src/arm64.
    Release tarballs omit it; re-run scripts/fetch-sources.sh (git clone)
    or point UBOOT_DIR at a complete checkout."
	fi
	if [ -n "$board_dts" ]; then
		log "board:   ${dt_base#"$UBOOT_DIR"/}/$board_dts.dts"
		[ -f "$dt_base/$board_dts.dts" ] ||
			die "${dt_base#"$UBOOT_DIR"/}/$board_dts.dts is missing.
    Run scripts/apply.sh to install the board DTS."
	else
		warn "$UBOOT_DEFCONFIG does not set CONFIG_DEFAULT_DEVICE_TREE"
	fi

	detect_cross_compile arm
	log "cross:   ${CROSS_COMPILE}gcc ($("${CROSS_COMPILE}gcc" -dumpversion 2>/dev/null || echo '?'))"
	log "jobs:    $JOBS"

	if [ "$DRY_RUN" = 0 ]; then
		need make
		need bison
		need flex
		need python3
	fi

	log "configuring ($UBOOT_DEFCONFIG)"
	ubuild "$UBOOT_DEFCONFIG"

	log "building (-j$JOBS)"
	ubuild -j"$JOBS"

	[ "$DRY_RUN" = 1 ] && { log "would stage artifacts into $ART_DIR"; return 0; }

	local f
	mkdir -p "$ART_DIR"
	for f in u-boot.bin u-boot.dtb u-boot-nodtb.bin; do
		[ -f "$BUILD_DIR/$f" ] || continue
		install -m 0644 "$BUILD_DIR/$f" "$ART_DIR/$f"
	done
	[ -s "$ART_DIR/u-boot.bin" ] || die "u-boot.bin was not produced in $BUILD_DIR"

	echo
	log "artifacts in $ART_DIR"
	for f in "$ART_DIR"/*.bin "$ART_DIR"/*.dtb; do
		[ -f "$f" ] || continue
		printf '    %10s  %s\n' "$(stat -c %s "$f")" "$f"
	done
	log "note: the Amlogic FIP (BL2/BL31 packaging) is built separately"
}

main "$@"
