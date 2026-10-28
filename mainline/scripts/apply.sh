#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Integrate the AtriOS mainline port into unpacked upstream trees.
#
#   scripts/apply.sh [--dry-run] [--kernel-only|--uboot-only] [--help]
#
# What it does, in this order (all steps idempotent):
#   kernel
#     1. mainline/dts/*.dts        -> arch/arm64/boot/dts/amlogic/
#     2. mainline/drivers/**/*.c,h -> drivers/... (same relative path);
#        mainline/drivers/audio/* -> sound/soc/codecs/ (ASoC codecs)
#     3. mainline/patches/linux/*.patch in name order
#     4. build wiring for the copied drivers (Kconfig/Makefile entries that
#        cannot come from a patch against upstream)
#   u-boot (skipped with a warning while mainline/u-boot/ is still empty)
#     1. mainline/u-boot/*.dts     -> arch/arm/dts/
#     2. mainline/u-boot/*_defconfig -> configs/
#     3. mainline/patches/uboot/*.patch in name order
#
# Patches are applied with "git apply" when the tree is a git checkout and
# with "patch -p1 --forward" otherwise.  A patch that is already applied is
# detected with a reverse dry-run and skipped, so re-running is safe.
#
# Environment:
#   SRC_DIR, KVER, UBOOT_VER        see scripts/lib.sh
#   LINUX_DIR, UBOOT_DIR            override the tree locations directly
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

export LC_ALL=C

DRY_RUN=0
KERNEL_ONLY=0
UBOOT_ONLY=0

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Copy the board DTS, the non-upstream drivers and the patches from this
repository into the unpacked Linux $KVER and U-Boot $UBOOT_VER trees.
Re-running is safe: copied files are overwritten only when they differ and
patches that are already applied are skipped.

Options:
  --dry-run      show what would change, modify nothing
  --kernel-only  only touch $LINUX_DIR
  --uboot-only   only touch $UBOOT_DIR
  -h, --help     this help

Environment:
  SRC_DIR=$SRC_DIR
  LINUX_DIR=$LINUX_DIR
  UBOOT_DIR=$UBOOT_DIR
  PATCH_FUZZ=${PATCH_FUZZ:-0}   extra fuzz levels passed to patch(1); git trees use git apply
  PATCH_SKIP=${PATCH_SKIP:-<none>}  whitespace separated patch names/globs to skip
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) DRY_RUN=1; shift ;;
		--kernel-only) KERNEL_ONLY=1; shift ;;
		--uboot-only) UBOOT_ONLY=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done

[ "$KERNEL_ONLY" = 1 ] && [ "$UBOOT_ONLY" = 1 ] &&
	die "--kernel-only and --uboot-only are mutually exclusive"

# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

install_file() { # SRC DST
	local src=$1 dst=$2
	if [ -f "$dst" ] && cmp -s "$src" "$dst"; then
		log "up to date: $dst"
		return 0
	fi
	if [ "$DRY_RUN" = 1 ]; then
		log "would install ${src#"$MAINLINE_DIR"/} -> $dst"
		return 0
	fi
	mkdir -p "$(dirname "$dst")"
	install -m 0644 "$src" "$dst"
	log "installed ${src#"$MAINLINE_DIR"/} -> $dst"
}

patch_applied() { # TREE PATCH
	local out
	if [ -d "$1/.git" ] && [ "${PATCH_FUZZ:-0}" = 0 ]; then
		git -C "$1" apply --reverse --check "$2" >/dev/null 2>&1
		return
	fi
	# patch(1) silently flips -R when it thinks the patch is "unreversed",
	# so a zero exit status alone does not mean "already applied"; look at
	# the message and treat the flip as "not applied".
	out=$(patch --dry-run --batch -R -p1 -d "$1" <"$2" 2>&1) || return 1
	case "$out" in
		*"Ignoring -R"*|*"Unreversed patch detected"*) return 1 ;;
	esac
	return 0
}

# A marker in the tree records which patches these scripts applied.  This
# makes re-runs idempotent even for patches that create files which apply.sh
# also copies from mainline/ (the reverse dry-run cannot match those).
patch_recorded() { # TREE HASH
	[ -f "$1/.atrios-mainline-patches" ] &&
		grep -q "^$2 " "$1/.atrios-mainline-patches"
}

patch_record() { # TREE HASH NAME
	[ "$DRY_RUN" = 1 ] && return 0
	printf '%s  %s\n' "$2" "$3" >>"$1/.atrios-mainline-patches"
}

# Check that a patch applies cleanly before touching the tree, so a stale or
# conflicting patch leaves no half-patched files behind.  --forward keeps
# patch(1) from silently "assuming -R" when a target file already exists.
patch_applies() { # TREE PATCH
	local out
	out=$(patch --dry-run --batch --forward --silent -p1 -F "${PATCH_FUZZ:-0}" \
		-d "$1" <"$2" 2>&1) || return 1
	case "$out" in
		*"Assuming -R"*|*"already exists"*|*"Reversed"*|*"Skipping patch"*)
			return 1 ;;
	esac
	return 0
}

apply_patch() { # TREE PATCH
	local tree=$1 p=$2
	local name=${p#"$MAINLINE_DIR"/} base=${p##*/} hash pat
	hash=$(sha256_of "$p")

	# Escape hatch for a known-stale patch while it is being refreshed:
	# PATCH_SKIP="0002-*" apply.sh ...
	# shellcheck disable=SC2086  # PATCH_SKIP is a whitespace separated list
	for pat in ${PATCH_SKIP:-}; do
		# shellcheck disable=SC2053  # glob match on purpose
		if [[ "$base" == $pat ]]; then
			warn "skipping $name (PATCH_SKIP=$pat)"
			return 0
		fi
	done

	if patch_recorded "$tree" "$hash"; then
		log "already applied: $name"
		return 0
	fi

	if patch_applied "$tree" "$p"; then
		log "already applied: $name"
		patch_record "$tree" "$hash" "$name"
		return 0
	fi

	if ! patch_applies "$tree" "$p"; then
		die "patch does not apply to $tree: $name
    Either the patch is stale against this source version, or it was updated
    after this tree applied it (see $tree/.atrios-mainline-patches).
    Nothing was changed.  Refresh the patch, reset the tree, or allow fuzz
    explicitly with PATCH_FUZZ=N if offsets alone are wrong."
	fi

	if [ "$DRY_RUN" = 1 ]; then
		log "would apply: $name"
		return 0
	fi

	if [ -d "$tree/.git" ] && [ "${PATCH_FUZZ:-0}" = 0 ]; then
		git -C "$tree" apply "$p" ||
			die "git apply failed: $name (tree: $tree)"
	else
		patch --batch --forward --no-backup-if-mismatch -F "${PATCH_FUZZ:-0}" \
			-p1 -d "$tree" <"$p" || die "patch failed: $name (tree: $tree)"
	fi
	patch_record "$tree" "$hash" "$name"
	log "applied: $name"
}

# --------------------------------------------------------------------------
# kernel
# --------------------------------------------------------------------------

ensure_ringfb_wiring() {
	local kc="$LINUX_DIR/drivers/leds/trigger/Kconfig"
	local mk="$LINUX_DIR/drivers/leds/trigger/Makefile"

	[ -f "$kc" ] || { warn "missing $kc, cannot wire ledtrig-ringfb"; return 0; }
	[ -f "$mk" ] || { warn "missing $mk, cannot wire ledtrig-ringfb"; return 0; }

	if grep -q '^config LEDS_TRIGGER_RINGFB' "$kc"; then
		log "Kconfig: LEDS_TRIGGER_RINGFB already present"
	elif [ "$DRY_RUN" = 1 ]; then
		log "would add LEDS_TRIGGER_RINGFB to drivers/leds/trigger/Kconfig"
	else
		cat >>"$kc" <<'EOF'

# AtriOS: framebuffer-backed trigger for the Station Max LED ring
# (drivers/leds/trigger/ledtrig-ringfb.c, not upstream).
config LEDS_TRIGGER_RINGFB
	tristate "LED framebuffer trigger (Yandex Station Max ring)"
	depends on LEDS_TRIGGERS
	depends on FB_CORE
	select FB_SYS_FILLRECT
	select FB_SYS_COPYAREA
	select FB_SYS_IMAGEBLIT
	select FB_SYSMEM_FOPS
	help
	  Framebuffer-backed LED trigger for the 24x RGB ring of the Yandex
	  Station Max.  Userspace writes RGB888 pixels to /dev/fbN and the
	  trigger pushes the buffer to the LED class devices on every change.
EOF
		log "added LEDS_TRIGGER_RINGFB to drivers/leds/trigger/Kconfig"
	fi

	if grep -q 'ledtrig-ringfb' "$mk"; then
		log "Makefile: ledtrig-ringfb already present"
	elif [ "$DRY_RUN" = 1 ]; then
		log "would add ledtrig-ringfb.o to drivers/leds/trigger/Makefile"
	else
		# shellcheck disable=SC2016  # Makefile variable, not shell
		printf 'obj-$(CONFIG_LEDS_TRIGGER_RINGFB) += ledtrig-ringfb.o\n' >>"$mk"
		log "added ledtrig-ringfb.o to drivers/leds/trigger/Makefile"
	fi
}

check_driver_wiring() { # RELPATH_UNDER_DRIVERS
	local rel=$1
	local base obj mk
	base=$(basename "$rel")
	case "$base" in
		*.h) return 0 ;;
	esac
	obj="${base%.c}.o"
	mk="$LINUX_DIR/drivers/$(dirname "$rel")/Makefile"
	if [ -f "$mk" ] && grep -q -- "$obj" "$mk"; then
		log "wired: drivers/$rel"
	else
		warn "drivers/$rel is copied but not referenced from drivers/$(dirname "$rel")/Makefile;"
		warn "    it will not be built until a Kconfig/Makefile entry exists"
	fi
}

apply_kernel() {
	if ! is_kernel_tree "$LINUX_DIR"; then
		die "Linux tree not found: $LINUX_DIR
    Run scripts/fetch-sources.sh first (or set LINUX_DIR)."
	fi

	log "kernel tree: $LINUX_DIR"

	# Patches first: several of them create driver files that mainline/ also
	# carries, so mainline/ is copied afterwards and wins.  The applied-set
	# marker keeps re-runs idempotent in that case.
	local p
	shopt -s nullglob
	local patches=("$MAINLINE_DIR"/patches/linux/*.patch)
	shopt -u nullglob
	if [ ${#patches[@]} -eq 0 ]; then
		log "no linux patches in mainline/patches/linux"
	fi
	for p in "${patches[@]}"; do
		apply_patch "$LINUX_DIR" "$p"
	done

	local f
	shopt -s nullglob
	local dts_files=("$MAINLINE_DIR"/dts/*.dts)
	shopt -u nullglob
	[ ${#dts_files[@]} -gt 0 ] || warn "mainline/dts/ has no .dts files"
	local -a dts_names=()
	for f in "${dts_files[@]}"; do
		local name
		name=$(basename "$f")
		install_file "$f" "$LINUX_DIR/arch/arm64/boot/dts/amlogic/$name"
		dts_names+=("${name%.dts}.dtb")
	done

	# A DTS that is copied into the tree is not built until it is listed in
	# the amlogic Makefile.
	local dts_mk="$LINUX_DIR/arch/arm64/boot/dts/amlogic/Makefile"
	if [ -f "$dts_mk" ] && [ ${#dts_names[@]} -gt 0 ]; then
		for name in "${dts_names[@]}"; do
			if grep -qF "$name" "$dts_mk"; then
				log "dtb entry present: $name"
			elif [ "$DRY_RUN" -eq 1 ]; then
				log "would add $name to arch/arm64/boot/dts/amlogic/Makefile"
			else
				printf 'dtb-$(CONFIG_ARCH_MESON) += %s\n' "$name" >> "$dts_mk"
				log "added dtb entry: $name"
			fi
		done
	fi

	local -a copied=()
	while IFS= read -r -d '' f; do
		local rel=${f#"$MAINLINE_DIR/drivers/"}
		case "$rel" in
		audio/*)
			# ASoC codecs belong to sound/soc/codecs; their Kconfig and
			# Makefile entries come from the matching patch.
			install_file "$f" "$LINUX_DIR/sound/soc/codecs/$(basename "$f")"
			continue
			;;
		esac
		install_file "$f" "$LINUX_DIR/drivers/$rel"
		copied+=("$rel")
	done < <(find "$MAINLINE_DIR/drivers" -type f \( -name '*.c' -o -name '*.h' \) -print0 2>/dev/null)

	# Wiring after the patches and copies so an upstream-style patch that
	# already adds the Kconfig entry wins over the generic fallback below.
	if [ -f "$LINUX_DIR/drivers/leds/trigger/ledtrig-ringfb.c" ]; then
		ensure_ringfb_wiring
	fi
	for f in "${copied[@]}"; do
		check_driver_wiring "$f"
	done
}

# --------------------------------------------------------------------------
# u-boot
# --------------------------------------------------------------------------

apply_uboot() {
	if ! is_uboot_tree "$UBOOT_DIR"; then
		if [ "$UBOOT_ONLY" = 1 ]; then
			die "U-Boot tree not found: $UBOOT_DIR
    Run scripts/fetch-sources.sh first (or set UBOOT_DIR)."
		fi
		warn "U-Boot tree not found ($UBOOT_DIR), skipping U-Boot integration"
		return 0
	fi

	log "u-boot tree: $UBOOT_DIR"

	local f name
	shopt -s nullglob
	local dts_files=("$MAINLINE_DIR"/u-boot/*.dts)
	local dtsi_files=("$MAINLINE_DIR"/u-boot/*.dtsi)
	local cfg_files=("$MAINLINE_DIR"/u-boot/*_defconfig "$MAINLINE_DIR"/u-boot/configs/*_defconfig)
	local patches=("$MAINLINE_DIR"/patches/uboot/*.patch)
	shopt -u nullglob

	if [ ${#dts_files[@]} -eq 0 ] && [ ${#cfg_files[@]} -eq 0 ]; then
		if [ "$UBOOT_ONLY" = 1 ]; then
			die "no U-Boot board files in mainline/u-boot/
    Expected yandex-station-max_defconfig and a board .dts from the U-Boot port."
		fi
		warn "no U-Boot board files in mainline/u-boot/ yet; nothing to integrate"
		return 0
	fi

	# The defconfig decides where the board DTS belongs: CONFIG_OF_UPSTREAM=y
	# means U-Boot builds dts/upstream/src/arm64/<CONFIG_DEFAULT_DEVICE_TREE>.dts,
	# otherwise arch/arm/dts/<...>.dts.
	local dt_base="$UBOOT_DIR/arch/arm/dts"
	local default_dt=""
	for f in "${cfg_files[@]}"; do
		name=$(basename "$f")
		install_file "$f" "$UBOOT_DIR/configs/$name"
		if [ -z "$default_dt" ]; then
			default_dt=$(sed -n 's/^CONFIG_DEFAULT_DEVICE_TREE="\(.*\)"/\1/p' "$f" | head -n1)
			if grep -q '^CONFIG_OF_UPSTREAM=y' "$f"; then
				dt_base="$UBOOT_DIR/dts/upstream/src/arm64"
			fi
		fi
		if ! grep -q '^CONFIG_DEFAULT_DEVICE_TREE=' "$f"; then
			warn "$name does not set CONFIG_DEFAULT_DEVICE_TREE;"
			warn "    U-Boot will not pick the board DTS"
		fi
	done

	if [ -n "$default_dt" ]; then
		log "board DTS target: ${dt_base#"$UBOOT_DIR"/}/$default_dt.dts"
		if [ "$dt_base" = "$UBOOT_DIR/dts/upstream/src/arm64" ] &&
			[ ! -d "$UBOOT_DIR/dts/upstream/src/arm64" ]; then
			warn "$UBOOT_DIR has no dts/upstream/src/arm64 (incomplete checkout);"
			warn "    re-run scripts/fetch-sources.sh - it clones U-Boot with git"
		fi
	fi

	for f in "${dts_files[@]}"; do
		name=$(basename "$f")
		if [ -n "$default_dt" ] && [ "$name" = "$(basename "$default_dt").dts" ]; then
			install_file "$f" "$dt_base/$default_dt.dts"
		else
			install_file "$f" "$dt_base/$name"
		fi
	done
	# U-Boot overlays live next to the upstream DTS in arch/arm/dts.
	for f in "${dtsi_files[@]}"; do
		install_file "$f" "$UBOOT_DIR/arch/arm/dts/$(basename "$f")"
	done

	if [ -d "$MAINLINE_DIR/u-boot/board" ]; then
		while IFS= read -r -d '' f; do
			install_file "$f" "$UBOOT_DIR/board/${f#"$MAINLINE_DIR/u-boot/board/"}"
		done < <(find "$MAINLINE_DIR/u-boot/board" -type f -print0)
		if [ -f "$UBOOT_DIR/board/amlogic/Makefile" ] &&
			! grep -q 'yandex-station-max' "$UBOOT_DIR/board/amlogic/Makefile"; then
			warn "board/amlogic/Makefile does not reference yandex-station-max/"
			warn "    add 'obj-\$(CONFIG_TARGET_YANDEX_STATION_MAX) += yandex-station-max/'"
		fi
	fi

	local p
	for p in "${patches[@]}"; do
		apply_patch "$UBOOT_DIR" "$p"
	done
}

# --------------------------------------------------------------------------

main() {
	[ "$UBOOT_ONLY" = 1 ] || apply_kernel
	[ "$KERNEL_ONLY" = 1 ] || apply_uboot
	log "done"
}

main "$@"
