#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# One-shot reproducible build of the mainline kernel and U-Boot.
#
#   scripts/build-all.sh [--jobs N] [--skip-fetch] [--skip-uboot] [--dry-run]
#
# Runs fetch-sources.sh, apply.sh, build-kernel.sh and build-uboot.sh in
# order, then prints every artifact with its size and sha256.
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

JOBS="${JOBS:-$(nproc)}"
DRY_RUN=0
SKIP_FETCH=0
SKIP_UBOOT=0
# A missing U-Boot board port must not block a kernel-only build.
ALLOW_UBOOT_FAIL="${ALLOW_UBOOT_FAIL:-0}"

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Fetch, patch and build Linux $KVER + U-Boot $UBOOT_VER for the
Yandex Station Max.  All output lands in $OUT_DIR.

Options:
  -j, --jobs N   parallel build jobs (default $JOBS)
      --skip-fetch  reuse the trees already in \$SRC_DIR
      --skip-uboot  build the kernel only
      --dry-run     print the plan, build nothing
  -h, --help     this help

Environment:
  SRC_DIR=$SRC_DIR
  OUT_DIR=$OUT_DIR
  CROSS_COMPILE=${CROSS_COMPILE:-<auto>}
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		-j|--jobs)
			[ -n "${2:-}" ] || die "--jobs requires a number"
			JOBS=$2; shift 2 ;;
		--skip-fetch) SKIP_FETCH=1; shift ;;
		--skip-uboot) SKIP_UBOOT=1; shift ;;
		--dry-run) DRY_RUN=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done

step() { # TITLE COMMAND...
	local title=$1
	shift
	echo
	log "=== $title ==="
	"$@"
}

print_artifacts() {
	local dir f
	for dir in "$OUT_DIR/linux" "$OUT_DIR/uboot"; do
		[ -d "$dir" ] || continue
		echo
		log "artifacts in $dir"
		while IFS= read -r f; do
			printf '    %10s  %s  %s\n' \
				"$(stat -c %s "$f")" "$(sha256_of "$f")" "${f#"$OUT_DIR"/}"
		done < <(find "$dir" -maxdepth 1 -type f | sort)
	done
}

main() {
	local dry=()
	[ "$DRY_RUN" = 1 ] && dry+=(--dry-run)

	[ "$SKIP_FETCH" = 1 ] ||
		step "fetch sources" "$MAINLINE_DIR/scripts/fetch-sources.sh" "${dry[@]}"

	step "apply board files and patches" "$MAINLINE_DIR/scripts/apply.sh" "${dry[@]}"

	step "build kernel" env JOBS="$JOBS" \
		"$MAINLINE_DIR/scripts/build-kernel.sh" "${dry[@]}"

	if [ "$SKIP_UBOOT" = 1 ]; then
		log "skipping U-Boot (--skip-uboot)"
	else
		if ! step "build u-boot" env JOBS="$JOBS" \
			"$MAINLINE_DIR/scripts/build-uboot.sh" "${dry[@]}"; then
			if [ "$ALLOW_UBOOT_FAIL" = 1 ]; then
				warn "U-Boot build failed (ALLOW_UBOOT_FAIL=1); kernel artifacts are still valid"
			else
				die "U-Boot build failed (set ALLOW_UBOOT_FAIL=1 to tolerate)"
			fi
		fi
	fi

	[ "$DRY_RUN" = 1 ] && { log "dry run complete, nothing was built"; return 0; }

	print_artifacts
}

main "$@"
