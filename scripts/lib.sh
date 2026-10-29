# shellcheck shell=bash
# SPDX-License-Identifier: GPL-2.0-only
# Shared helpers for the mainline build scripts.  Sourced, never executed.
#
# All scripts in this directory resolve their paths from this file, so they
# work from any working directory and from any git worktree checkout.

MAINLINE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Where tarballs and unpacked trees live.  Shared by fetch/apply/build so a
# single export is enough to relocate the whole build.
SRC_DIR="${SRC_DIR:-/home/leftymods/mainline-src}"
OUT_DIR="${OUT_DIR:-$MAINLINE_DIR/out}"

KVER="${KVER:-7.3}"
UBOOT_VER="${UBOOT_VER:-2026.10}"

LINUX_DIR="${LINUX_DIR:-$SRC_DIR/linux-$KVER}"
UBOOT_DIR="${UBOOT_DIR:-$SRC_DIR/u-boot-$UBOOT_VER}"

log()  { printf '[+] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*" >&2; }
die()  { printf '[x] %s\n' "$*" >&2; exit 1; }

need() {
	command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1"
}

sha256_of() {
	sha256sum "$1" | awk '{print $1}'
}

# verify_archive FILE -> 0 if the compressed stream is intact
verify_archive() {
	case "$1" in
		*.tar.xz|*.xz) xz -t "$1" ;;
		*.tar.gz|*.tgz) gzip -t "$1" ;;
		*.tar.bz2) bzip2 -t "$1" ;;
		*) return 1 ;;
	esac
}

# detect_cross_compile [arch] -> sets and prints CROSS_COMPILE
# arch is only used in the error message; both trees are built for aarch64.
detect_cross_compile() {
	if [ -n "${CROSS_COMPILE:-}" ]; then
		"${CROSS_COMPILE}gcc" --version >/dev/null 2>&1 ||
			die "CROSS_COMPILE=${CROSS_COMPILE}: ${CROSS_COMPILE}gcc does not run"
		return 0
	fi

	local prefix
	for prefix in aarch64-linux-gnu- aarch64-none-linux-gnu- aarch64-unknown-linux-gnu-; do
		if command -v "${prefix}gcc" >/dev/null 2>&1; then
			CROSS_COMPILE="$prefix"
			return 0
		fi
	done

	die "no aarch64 cross compiler found.
    Install it (Ubuntu/Debian): sudo apt-get install gcc-aarch64-linux-gnu
    or point CROSS_COMPILE at an existing toolchain prefix (with trailing dash)."
}

# is_kernel_tree DIR
# init/main.c and drivers/Makefile are unpacked late, so this does not accept
# a half-extracted tarball as a usable tree.
is_kernel_tree() {
	[ -f "$1/Makefile" ] && [ -f "$1/init/main.c" ] && [ -d "$1/arch/arm64" ]
}

# is_uboot_tree DIR
is_uboot_tree() {
	[ -f "$1/Makefile" ] && [ -d "$1/arch/arm" ]
}
