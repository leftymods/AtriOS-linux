#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build the mainline arm64 kernel for the Yandex Station Max.
#
#   scripts/build-kernel.sh [--jobs N] [--dry-run] [--strict] [--help]
#
# Steps: defconfig -> configs/atrios.config merged in -> olddefconfig ->
#        Image.gz + dtbs + modules -> artifacts in out/linux/.
#
# Environment:
#   SRC_DIR, KVER          see scripts/lib.sh
#   LINUX_DIR              kernel tree      (default $SRC_DIR/linux-$KVER)
#   OUT_DIR                artifact root    (default mainline/out)
#   CROSS_COMPILE          toolchain prefix (auto-detected aarch64-*-gcc)
#   JOBS                   parallel jobs    (default nproc)
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

JOBS="${JOBS:-$(nproc)}"
BUILD_DIR="${BUILD_DIR:-$OUT_DIR/linux/build}"
ART_DIR="$OUT_DIR/linux"
CONFIG_FRAGMENT="$MAINLINE_DIR/configs/atrios.config"
BOARD_DTB="${BOARD_DTB:-meson-sm1-yandex-station-max.dtb}"
if [ -z "${DEPMOD:-}" ]; then
	if command -v depmod >/dev/null 2>&1; then
		DEPMOD=depmod
	else
		DEPMOD=true	# modules.tar still works, only modules.dep is skipped
	fi
fi

DRY_RUN=0
STRICT=0

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Build Linux $KVER for the Yandex Station Max (arm64/Amlogic SM1).

Options:
  -j, --jobs N   parallel build jobs (default $JOBS)
      --dry-run  print the commands that would run, build nothing
      --strict   fail if a symbol from configs/atrios.config is not set after
                 olddefconfig (by default this is only a warning)
  -h, --help     this help

Environment:
  LINUX_DIR=$LINUX_DIR
  BUILD_DIR=$BUILD_DIR
  CROSS_COMPILE=${CROSS_COMPILE:-<auto>}
  OUT_DIR=$OUT_DIR

Artifacts:
  $ART_DIR/Image.gz
  $ART_DIR/$BOARD_DTB (and the other meson-sm1-*.dtb)
  $ART_DIR/modules.tar
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		-j|--jobs)
			[ -n "${2:-}" ] || die "--jobs requires a number"
			JOBS=$2; shift 2 ;;
		--dry-run) DRY_RUN=1; shift ;;
		--strict) STRICT=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done

kbuild() {
	if [ "$DRY_RUN" = 1 ]; then
		printf '    make -C %s O=%s ARCH=arm64 CROSS_COMPILE=%s %s\n' \
			"$LINUX_DIR" "$BUILD_DIR" "$CROSS_COMPILE" "$*"
		return 0
	fi
	make -C "$LINUX_DIR" O="$BUILD_DIR" ARCH=arm64 \
		CROSS_COMPILE="$CROSS_COMPILE" "$@"
}

check_fragment() {
	local line sym want got missing=0
	while IFS= read -r line; do
		sym=${line%%=*}
		want=${line#*=}
		case "$want" in
			n)
				got=$(grep -m1 "^# $sym is not set$" "$BUILD_DIR/.config" || true)
				[ -n "$got" ] || { warn "fragment asks for $sym=n but it is set"; missing=1; }
				;;
			*)
				got=$(grep -m1 "^$sym=" "$BUILD_DIR/.config" || true)
				if [ -z "$got" ]; then
					warn "not set after olddefconfig: $line"
					missing=1
				elif [ "$got" != "$line" ]; then
					warn "value differs: wanted $line, got $got"
				fi
				;;
		esac
	done < <(grep -E '^CONFIG_[A-Za-z0-9_]+=' "$CONFIG_FRAGMENT")
	return "$missing"
}

stage_artifacts() {
	local dtbdir="$BUILD_DIR/arch/arm64/boot/dts/amlogic"
	local -a dtbs

	mkdir -p "$ART_DIR"
	[ -f "$BUILD_DIR/arch/arm64/boot/Image.gz" ] ||
		die "$BUILD_DIR/arch/arm64/boot/Image.gz was not produced"
	install -m 0644 "$BUILD_DIR/arch/arm64/boot/Image.gz" "$ART_DIR/Image.gz"

	[ -f "$dtbdir/$BOARD_DTB" ] ||
		die "$dtbdir/$BOARD_DTB was not built. Is the board DTS in the tree?
    Run scripts/apply.sh, then 'make dtbs' by hand to see the dtc error."

	shopt -s nullglob
	dtbs=("$dtbdir"/meson-sm1-*.dtb)
	shopt -u nullglob
	cp -f "${dtbs[@]}" "$ART_DIR/"
	log "staged ${#dtbs[@]} dtb(s)"

	rm -rf "$ART_DIR/modules"
	mkdir -p "$ART_DIR/modules"
	log "installing modules (INSTALL_MOD_STRIP=1)"
	kbuild INSTALL_MOD_PATH="$ART_DIR/modules" INSTALL_MOD_STRIP=1 \
		DEPMOD="$DEPMOD" modules_install
	tar -czf "$ART_DIR/modules.tar" \
		--exclude='*/build' --exclude='*/source' \
		-C "$ART_DIR/modules" .
	rm -rf "$ART_DIR/modules"
}

print_artifacts() {
	local f
	echo
	log "artifacts in $ART_DIR"
	for f in "$ART_DIR"/Image.gz "$ART_DIR"/modules.tar "$ART_DIR"/*.dtb; do
		[ -f "$f" ] || continue
		printf '    %10s  %s\n' "$(stat -c %s "$f")" "$f"
	done
}

main() {
	[ -f "$CONFIG_FRAGMENT" ] || die "missing config fragment: $CONFIG_FRAGMENT"
	[ -f "$LINUX_DIR/Makefile" ] || die "Linux tree not found: $LINUX_DIR
    Run scripts/fetch-sources.sh first (or set LINUX_DIR)."
	[ -f "$LINUX_DIR/arch/arm64/boot/dts/amlogic/${BOARD_DTB%.dtb}.dts" ] ||
		warn "board DTS ${BOARD_DTB%.dtb}.dts is not in $LINUX_DIR yet; run scripts/apply.sh"

	detect_cross_compile arm64
	log "kernel:  $LINUX_DIR"
	log "build:   $BUILD_DIR"
	log "cross:   ${CROSS_COMPILE}gcc ($("${CROSS_COMPILE}gcc" -dumpversion 2>/dev/null || echo '?'))"
	log "jobs:    $JOBS"

	if [ "$DRY_RUN" = 1 ]; then
		log "dry run, nothing will be built"
	else
		need make
		need bc
		need bison
		need flex
		need tar
	fi

	log "defconfig"
	kbuild defconfig

	log "merging configs/atrios.config"
	if [ "$DRY_RUN" = 1 ]; then
		printf '    %s -m -O %s %s %s\n' \
			"$LINUX_DIR/scripts/kconfig/merge_config.sh" "$BUILD_DIR" \
			"$BUILD_DIR/.config" "$CONFIG_FRAGMENT"
	else
		[ -x "$LINUX_DIR/scripts/kconfig/merge_config.sh" ] ||
			die "missing $LINUX_DIR/scripts/kconfig/merge_config.sh"
		"$LINUX_DIR/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" \
			"$BUILD_DIR/.config" "$CONFIG_FRAGMENT"
	fi
	kbuild olddefconfig

	if [ "$DRY_RUN" = 0 ]; then
		if ! check_fragment; then
			warn "some fragment symbols are missing or differ (see above)"
			[ "$STRICT" = 1 ] && die "--strict: config fragment not fully applied"
		fi
	fi

	log "building Image.gz + dtbs + modules (-j$JOBS)"
	kbuild -j"$JOBS" Image.gz dtbs modules

	if [ "$DRY_RUN" = 1 ]; then
		log "would stage artifacts into $ART_DIR"
		return 0
	fi

	stage_artifacts
	print_artifacts
}

main "$@"
