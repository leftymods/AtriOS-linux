#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Fetch, verify and unpack the upstream sources for the AtriOS mainline port.
#
#   scripts/fetch-sources.sh [--force] [--dry-run] [--strict] [--help]
#
# Linux is downloaded as a tarball and checked against the kernel.org sha256
# and size; U-Boot is cloned with git because the release tarballs omit the
# dts/upstream subtree that CONFIG_OF_UPSTREAM boards need (set
# UBOOT_USE_TARBALL=1 to use the tarball anyway).
#
# Idempotent: an archive that is already complete and matches its checksum is
# reused, an unpacked tree is left alone, and a partial download is resumed
# with curl --continue-at.
#
# Environment:
#   SRC_DIR       download/tree location   (default /home/leftymods/mainline-src)
#   KVER          kernel version           (default 7.3)
#   UBOOT_VER     U-Boot version           (default 2026.10)
#   LINUX_MIRROR  kernel mirror base       (default https://cdn.kernel.org/pub/linux/kernel)
#   LINUX_SHA256  expected kernel hash     (default pinned from kernel.org)
#   LINUX_SIZE    expected kernel size     (default 162399360, from the mirror)
#   UBOOT_SHA256  expected U-Boot hash     (default empty: hash is printed, not pinned)
#   STRICT=1      fail when the U-Boot hash is not pinned (same as --strict)
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

# Pinned from https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc
LINUX_SHA256="${LINUX_SHA256:-87363c24f10e06321eb362765521094e94471f6bff8bce2d6b3e40f39f004953}"
LINUX_SIZE="${LINUX_SIZE:-162399360}"
UBOOT_SHA256="${UBOOT_SHA256:-}"
STRICT="${STRICT:-0}"

FORCE=0
DRY_RUN=0

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Download, verify (sha256 + size) and unpack Linux $KVER and U-Boot $UBOOT_VER
into \$SRC_DIR.  Safe to re-run: complete archives are reused, partial ones are
resumed, already unpacked trees are skipped.

Options:
  --force      re-download the archive even if it is already there
  --dry-run    print what would be done, touch nothing, no network access
  --strict     fail if a checksum is not pinned (U-Boot has no upstream sha256)
  -h, --help   this help

Environment:
  SRC_DIR=$SRC_DIR
  KVER=$KVER  UBOOT_VER=$UBOOT_VER
  LINUX_SHA256=${LINUX_SHA256:-<unpinned>}
  UBOOT_SHA256=${UBOOT_SHA256:-<unpinned>}
  UBOOT_USE_TARBALL=${UBOOT_USE_TARBALL:-0}  (1: tarball instead of git clone)
  UBOOT_GIT_URL=${UBOOT_GIT_URL:-https://github.com/u-boot/u-boot.git}

Resulting trees:
  $LINUX_DIR
  $UBOOT_DIR
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--force) FORCE=1; shift ;;
		--dry-run) DRY_RUN=1; shift ;;
		--strict) STRICT=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done

[ "$STRICT" = 1 ] && [ -z "$UBOOT_SHA256" ] &&
	die "--strict: UBOOT_SHA256 is not pinned; upstream publishes no hash for
    the GitHub tag tarball.  Compute it once and re-run with UBOOT_SHA256=<hash>."

LINUX_ARCHIVE="$SRC_DIR/linux-$KVER.tar.xz"
LINUX_URLS=(
	"${LINUX_MIRROR:-https://cdn.kernel.org/pub/linux/kernel}/v${KVER%%.*}.x/linux-$KVER.tar.xz"
	"https://mirrors.edge.kernel.org/pub/linux/kernel/v${KVER%%.*}.x/linux-$KVER.tar.xz"
	"https://www.kernel.org/pub/linux/kernel/v${KVER%%.*}.x/linux-$KVER.tar.xz"
)

# U-Boot is cloned by default: the release tarballs do not carry the
# dts/upstream subtree that CONFIG_OF_UPSTREAM boards (including this one)
# build their device tree from.  The tarball stays as an offline fallback,
# with a clear failure if dts/upstream is missing.
UBOOT_GIT_URLS=(
	"${UBOOT_GIT_URL:-https://github.com/u-boot/u-boot.git}"
	"https://source.denx.de/u-boot/u-boot.git"
)

# U-Boot tarballs: name differs per source, so URL and destination are paired.
UBOOT_URLS=(
	"https://ftp.denx.de/pub/u-boot/u-boot-$UBOOT_VER.tar.bz2"
	"https://github.com/u-boot/u-boot/archive/refs/tags/v$UBOOT_VER.tar.gz"
	"https://source.denx.de/u-boot/u-boot/-/archive/v$UBOOT_VER/u-boot-v$UBOOT_VER.tar.gz"
)
UBOOT_FILES=(
	"$SRC_DIR/u-boot-$UBOOT_VER.tar.bz2"
	"$SRC_DIR/u-boot-$UBOOT_VER.tar.gz"
	"$SRC_DIR/u-boot-v$UBOOT_VER.tar.gz"
)

download_to() { # DEST URL...
	local dest=$1 url
	shift
	for url in "$@"; do
		log "fetching $url"
		if curl -fL --retry 5 --retry-delay 3 --connect-timeout 20 \
				--continue-at - --output "$dest" "$url"; then
			return 0
		fi
		warn "download failed: $url"
	done
	return 1
}

check_sha256() { # FILE EXPECTED LABEL
	local file=$1 expected=$2 label=$3 got
	if [ -z "$expected" ]; then
		got=$(sha256_of "$file")
		if [ "$STRICT" = 1 ]; then
			die "$label: sha256 not pinned (computed $got)"
		fi
		warn "$label: sha256 not pinned, computed $got"
		warn "$label: pin it with ${label}_SHA256=$got for reproducible builds"
		return 0
	fi
	got=$(sha256_of "$file")
	[ "$got" = "$expected" ] ||
		die "$label: sha256 mismatch
    file:     $file
    expected: $expected
    actual:   $got
    Remove the file or re-run with --force."
	log "$label: sha256 ok ($got)"
}

ensure_linux_archive() {
	local archive=$LINUX_ARCHIVE actual

	if [ "$DRY_RUN" = 1 ]; then
		if [ -s "$archive" ]; then
			actual=$(stat -c %s "$archive")
			if [ "$LINUX_SIZE" -gt 0 ] && [ "$actual" -lt "$LINUX_SIZE" ]; then
				log "would resume $archive ($actual/$LINUX_SIZE bytes)"
			else
				log "would verify and reuse $archive"
			fi
		else
			log "would download $archive (size $LINUX_SIZE, sha256 $LINUX_SHA256)"
		fi
		return 0
	fi

	if [ "$FORCE" = 1 ] && [ -e "$archive" ]; then
		log "removing $archive (--force)"
		rm -f "$archive"
	fi

	if [ -s "$archive" ]; then
		actual=$(stat -c %s "$archive")
		if [ "$LINUX_SIZE" -gt 0 ] && [ "$actual" -gt "$LINUX_SIZE" ]; then
			die "$archive is larger than expected ($actual > $LINUX_SIZE bytes).
    Refusing to touch it; remove it yourself or re-run with --force."
		fi
		if [ "$LINUX_SIZE" -gt 0 ] && [ "$actual" -lt "$LINUX_SIZE" ]; then
			log "resuming partial download ($actual/$LINUX_SIZE bytes)"
			download_to "$archive" "${LINUX_URLS[@]}" ||
				die "could not resume $archive"
		elif ! xz -t "$archive" 2>/dev/null; then
			warn "$archive is corrupt, downloading again"
			rm -f "$archive"
			download_to "$archive" "${LINUX_URLS[@]}" ||
				die "could not download linux-$KVER.tar.xz"
		fi
	else
		download_to "$archive" "${LINUX_URLS[@]}" ||
			die "could not download linux-$KVER.tar.xz from any mirror"
	fi

	actual=$(stat -c %s "$archive")
	if [ "$LINUX_SIZE" -gt 0 ] && [ "$actual" -ne "$LINUX_SIZE" ]; then
		die "$archive: unexpected size $actual (want $LINUX_SIZE)"
	fi
	xz -t "$archive" || die "$archive: xz integrity check failed"
	check_sha256 "$archive" "$LINUX_SHA256" "LINUX"
}

ensure_uboot_archive() { # sets UBOOT_ARCHIVE
	local i f candidates=()

	# Reuse an already complete archive, in order of preference.
	for f in "${UBOOT_FILES[@]}"; do
		[ -s "$f" ] || continue
		if verify_archive "$f" 2>/dev/null; then
			candidates+=("$f")
		fi
	done
	if [ ${#candidates[@]} -gt 0 ]; then
		UBOOT_ARCHIVE=${candidates[0]}
		log "using existing archive $UBOOT_ARCHIVE"
		if [ -n "$UBOOT_SHA256" ]; then
			check_sha256 "$UBOOT_ARCHIVE" "$UBOOT_SHA256" "UBOOT"
		else
			log "sha256: $(sha256_of "$UBOOT_ARCHIVE") (unpinned)"
		fi
		return 0
	fi

	if [ "$DRY_RUN" = 1 ]; then
		log "would download U-Boot $UBOOT_VER from ${UBOOT_URLS[0]}"
		UBOOT_ARCHIVE=${UBOOT_FILES[0]}
		return 0
	fi

	for i in "${!UBOOT_URLS[@]}"; do
		f=${UBOOT_FILES[$i]}
		[ "$FORCE" = 1 ] && rm -f "$f"
		log "fetching ${UBOOT_URLS[$i]}"
		if ! curl -fL --retry 3 --retry-delay 3 --connect-timeout 20 \
				--continue-at - --output "$f" "${UBOOT_URLS[$i]}"; then
			warn "download failed: ${UBOOT_URLS[$i]}"
			rm -f "$f"
			continue
		fi
		if verify_archive "$f" 2>/dev/null; then
			UBOOT_ARCHIVE=$f
			check_sha256 "$f" "$UBOOT_SHA256" "UBOOT"
			return 0
		fi
		warn "archive is truncated or corrupt, trying the next source"
		rm -f "$f"
	done

	die "could not download U-Boot $UBOOT_VER from any source:
$(printf '    %s\n' "${UBOOT_URLS[@]}")"
}

unpack_linux() {
	if is_kernel_tree "$LINUX_DIR"; then
		log "already unpacked: $LINUX_DIR"
		return 0
	fi
	[ "$DRY_RUN" = 1 ] && { log "would unpack $LINUX_ARCHIVE -> $LINUX_DIR"; return 0; }
	log "unpacking $(basename "$LINUX_ARCHIVE")"
	tar -xf "$LINUX_ARCHIVE" -C "$SRC_DIR" || die "tar failed for $LINUX_ARCHIVE"
	is_kernel_tree "$LINUX_DIR" || die "expected $LINUX_DIR after unpacking"
}

unpack_uboot() { # ARCHIVE
	local archive=$1
	if is_uboot_tree "$UBOOT_DIR"; then
		log "already unpacked: $UBOOT_DIR"
		return 0
	fi
	[ "$DRY_RUN" = 1 ] && { log "would unpack $archive -> $UBOOT_DIR"; return 0; }
	log "unpacking $(basename "$archive")"
	tar -xf "$archive" -C "$SRC_DIR" || die "tar failed for $archive"
	# source.denx.de names the top directory u-boot-v<ver>.
	if [ ! -d "$UBOOT_DIR" ] && [ -d "$SRC_DIR/u-boot-v$UBOOT_VER" ]; then
		mv "$SRC_DIR/u-boot-v$UBOOT_VER" "$UBOOT_DIR"
	fi
	is_uboot_tree "$UBOOT_DIR" || die "expected $UBOOT_DIR after unpacking"
}

uboot_tree_complete() {
	is_uboot_tree "$1" && [ -d "$1/dts/upstream/src" ]
}

clone_uboot() {
	local tmp="$UBOOT_DIR.clone.tmp" url
	rm -rf "$tmp"
	for url in "${UBOOT_GIT_URLS[@]}"; do
		log "git clone --depth 1 --branch v$UBOOT_VER $url"
		if git clone --depth 1 --branch "v$UBOOT_VER" "$url" "$tmp"; then
			mv "$tmp" "$UBOOT_DIR"
			return 0
		fi
		warn "git clone failed: $url"
		rm -rf "$tmp"
	done
	return 1
}

ensure_uboot_tree() {
	if uboot_tree_complete "$UBOOT_DIR"; then
		log "already unpacked: $UBOOT_DIR"
		return 0
	fi

	if [ -d "$UBOOT_DIR" ]; then
		if [ "$DRY_RUN" = 1 ]; then
			warn "$UBOOT_DIR exists but is incomplete (no dts/upstream/src);"
			warn "    a real run would stop here and ask you to remove it"
			return 0
		fi
		die "$UBOOT_DIR exists but is incomplete (no dts/upstream/src).
    Release tarballs omit that subtree, and CONFIG_OF_UPSTREAM=y needs it.
    Remove the directory yourself and re-run; nothing was changed:
      rm -rf $UBOOT_DIR && $(basename "$0")"
	fi

	if [ "$DRY_RUN" = 1 ]; then
		log "would git clone v$UBOOT_VER into $UBOOT_DIR"
		return 0
	fi

	if [ "${UBOOT_USE_TARBALL:-0}" != 1 ] && command -v git >/dev/null 2>&1; then
		clone_uboot && return 0
		warn "falling back to the release tarball"
	fi

	UBOOT_ARCHIVE=""
	ensure_uboot_archive
	unpack_uboot "$UBOOT_ARCHIVE"

	if ! uboot_tree_complete "$UBOOT_DIR"; then
		die "$UBOOT_DIR has no dts/upstream/src.
    The release tarball cannot build CONFIG_OF_UPSTREAM boards.
    Re-run without UBOOT_USE_TARBALL=1 so the tree is cloned with git."
	fi
}

print_summary() {
	echo
	if [ "$DRY_RUN" = 1 ]; then
		log "dry run: nothing was fetched or unpacked"
	else
		log "sources ready"
	fi
	printf '    %-10s %s\n' "linux" "$LINUX_DIR"
	printf '    %-10s %s\n' "u-boot" "$UBOOT_DIR"
	printf '    %-10s %s\n' "srcdir" "$SRC_DIR"
}

main() {
	need curl
	need tar
	need sha256sum
	need xz
	need bzip2
	need gzip

	if [ "$DRY_RUN" = 0 ]; then
		mkdir -p "$SRC_DIR"
		if command -v flock >/dev/null 2>&1; then
			exec 9>"$SRC_DIR/.fetch.lock"
			flock -n 9 ||
				die "another fetch-sources.sh is already running on $SRC_DIR"
		fi
	fi

	ensure_linux_archive
	unpack_linux

	ensure_uboot_tree

	print_summary
}

main "$@"
