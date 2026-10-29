#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Assemble the INTERMEDIATE test image: vendor U-Boot 2015.01 (BL33) +
# Linux 7.3 (mainline) + the reusable Buildroot userspace.
#
#   scripts/make-image.sh [options]
#
# The vendor bootloader is kept on purpose: it is the only boot chain that is
# already known to work on the Station Max.  The kernel, its DTB and the
# modules come from out/linux; boot.PARTITION is rebuilt in the Android boot
# image format the vendor U-Boot parses (page 2048, kernel at page 1,
# DTB as the "second stage"), system.PARTITION is the Buildroot rootfs with
# /lib/modules/<kver> overlaid.
#
# This is NOT the final mainline image: BL33 is still the 2015 vendor
# U-Boot and the userspace is still the 4.9 Buildroot tree.
#
# Environment:
#   OUT_DIR, MAINLINE_DIR   see scripts/lib.sh
#   ATRIOS_PACK             vendor assets root (default /home/leftymods/atrios-pack)
#   ROOTFS_TAR              Buildroot rootfs tarball
#   MODULES_TAR             kernel modules tarball (default out/linux/modules.tar)
#   KERNELRELEASE           module directory name (auto-detected from modules.tar)
#   SYSTEM_SIZE             system.PARTITION size in bytes (default 1551892480)
#   VERSION                 image version (default <repo>/VERSION)
#   ATRIOS_NO_FAKEROOT=1    do not re-exec under fakeroot
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

ATRIOS_PACK="${ATRIOS_PACK:-/home/leftymods/atrios-pack}"
VENDOR_STAGING="${VENDOR_STAGING:-$ATRIOS_PACK/tools/fw_builder/staging}"
VENDOR_TOOLS="${VENDOR_TOOLS:-$ATRIOS_PACK/tools/bin}"

IMAGES_DIR="${IMAGES_DIR:-$OUT_DIR/images}"
STAGING_DIR="$IMAGES_DIR/staging"
KERNEL_IMG="$OUT_DIR/linux/Image.gz"
BOARD_DTB="$OUT_DIR/linux/meson-sm1-yandex-station-max.dtb"
MODULES_TAR="$OUT_DIR/linux/modules.tar"
ROOTFS_TAR="${ROOTFS_TAR:-/home/leftymods/rootfs-backup-20261027.tar}"
CMDLINE_FILE="${CMDLINE_FILE:-$ATRIOS_PACK/tools/fw_builder/overlay/boot/cmdline.txt}"
SYSTEM_SIZE="${SYSTEM_SIZE:-1551892480}"
# The vendor keeps kernel_addr=0x01080000, but its Image is only 29 MB and
# ends below U-Boot's loadaddr=0x0308000.  Our Image is 45 MB, so 0x01080000
# would be overwritten while U-Boot decompresses the boot image sitting at
# 0x0308000 (android_image_need_move() only relocates when the two addresses
# are closer than 32 MB).  The DTB also reserves secmon at
# 0x05000000-0x07300000.  0x20000000 is 2 MB aligned, far above both, and
# still below 1 GB so it also works on 1 GB boards.
KERNEL_ADDR="${KERNEL_ADDR:-0x20000000}"
KREL="${KERNELRELEASE:-}"
VERSION="${VERSION:-$(tr -d '[:space:]' < "$MAINLINE_DIR/../VERSION" 2>/dev/null || echo 0)}"
NO_ROOTFS=0
FAKE_ROOT=0
STAGING_SET=0

# Vendor assets that are repacked byte-for-byte.  boot.PARTITION and
# system.PARTITION are rebuilt separately, and the dtb partition is rebuilt
# from the mainline DTB (see build_dtb_container).
VENDOR_ITEMS=(
	bootloader.PARTITION
	UBOOT.USB
	DDR.USB
	aml_sdc_burn.UBOOT
	aml_sdc_burn.ini
	platform.conf
	logo.PARTITION
)
# The vendor U-Boot reads its kernel DTB from the "dtb" partition
# (store dtb read ${dtb_mem_addr}), not from boot.PARTITION: keep the AML_
# multi-DTB container header (it carries the board id checkhw matches) but
# replace its payload with our DTB.
VENDOR_DTB_TEMPLATE="$VENDOR_STAGING/_aml_dtb.PARTITION"
DTB_ASSETS=(_aml_dtb.PARTITION meson1.dtb meson1_ENC.dtb)

STAGING_OWNER_SUFFIX=.atrios-test-staging

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Build the intermediate "vendor BL33 + mainline kernel" test image.

Options:
      --out DIR       output directory (default $IMAGES_DIR)
      --staging DIR   staging directory, wiped and rebuilt (default $STAGING_DIR)
      --rootfs TAR    Buildroot rootfs tarball (default $ROOTFS_TAR)
      --kernel FILE   compressed kernel (default $KERNEL_IMG)
      --dtb FILE      board DTB (default $BOARD_DTB)
      --cmdline FILE  kernel command line (default $CMDLINE_FILE)
      --no-rootfs     reuse the vendor staging system.PARTITION, do not
                      unpack --rootfs (the kernel modules are then missing)
      --kver VER      module directory name (default: auto from modules.tar)
      --kernel-addr A physical kernel load address written into boot.PARTITION
                      (default $KERNEL_ADDR, must be 2 MB aligned)
      --fakeroot      re-exec under fakeroot to preserve uid/gid in the ext4
                      image (default: on when not running as root)
  -h, --help          this help

Environment:
  ATRIOS_PACK=$ATRIOS_PACK
  VENDOR_STAGING=$VENDOR_STAGING
  VENDOR_TOOLS=$VENDOR_TOOLS
  ROOTFS_TAR=$ROOTFS_TAR
  MODULES_TAR=$MODULES_TAR
  SYSTEM_SIZE=$SYSTEM_SIZE

Artifacts:
  <out>/atrios-mainline-test-v$VERSION.img
  <out>/atrios-mainline-test-v$VERSION.img.sha256
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--out)
			[ -n "${2:-}" ] || die "--out requires a directory"
			IMAGES_DIR=$2; shift 2 ;;
		--staging)
			[ -n "${2:-}" ] || die "--staging requires a directory"
			STAGING_DIR=$2; STAGING_SET=1; shift 2 ;;
		--rootfs)
			[ -n "${2:-}" ] || die "--rootfs requires a tarball"
			ROOTFS_TAR=$2; shift 2 ;;
		--kernel)
			[ -n "${2:-}" ] || die "--kernel requires a file"
			KERNEL_IMG=$2; shift 2 ;;
		--dtb)
			[ -n "${2:-}" ] || die "--dtb requires a file"
			BOARD_DTB=$2; shift 2 ;;
		--cmdline)
			[ -n "${2:-}" ] || die "--cmdline requires a file"
			CMDLINE_FILE=$2; shift 2 ;;
		--kver)
			[ -n "${2:-}" ] || die "--kver requires a version"
			KREL=$2; shift 2 ;;
		--kernel-addr)
			[ -n "${2:-}" ] || die "--kernel-addr requires an address"
			KERNEL_ADDR=$2; shift 2 ;;
		--no-rootfs) NO_ROOTFS=1; shift ;;
		--fakeroot) FAKE_ROOT=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done

# Normalise trailing slashes: the staging ownership marker is a sibling of
# the staging directory, so "/path/staging/" must not turn it into
# "/path/staging/.atrios-test-staging" (which ampack would try to pack).
[ "$IMAGES_DIR" = "/" ] || IMAGES_DIR=${IMAGES_DIR%/}
if [ "$STAGING_SET" = 1 ]; then
	[ "$STAGING_DIR" = "/" ] || STAGING_DIR=${STAGING_DIR%/}
else
	STAGING_DIR="$IMAGES_DIR/staging"
fi

# fakeroot keeps uid 0/gid 0 (and the setuid bits) in the ext4 image without
# requiring root.  One fakeroot session must cover tar, depmod and
# make_ext4fs, so re-exec the whole script instead of wrapping single
# commands.  ATRIOS_NO_FAKEROOT=1 turns this off (files then end up owned by
# the invoking user).
if [ "$FAKE_ROOT" = 1 ] && [ "${ATRIOS_NO_FAKEROOT:-0}" != 1 ] &&
	[ "$(id -u)" != 0 ] && [ -z "${FAKEROOTKEY:-}" ]; then
	if command -v fakeroot >/dev/null 2>&1; then
		exec fakeroot -- "$0" "$@"
	fi
	warn "fakeroot not found; rootfs file ownership will be $(id -u):$(id -g)"
fi

# Initial --fakeroot handling: when invoked from a normal shell, only enable
# it automatically for the rootfs path.
if [ "$FAKE_ROOT" = 0 ] && [ "$NO_ROOTFS" = 0 ] && [ "$(id -u)" != 0 ] &&
	[ "${ATRIOS_NO_FAKEROOT:-0}" != 1 ] && [ -z "${FAKEROOTKEY:-}" ] &&
	command -v fakeroot >/dev/null 2>&1; then
	exec fakeroot -- "$0" --fakeroot "$@"
fi

detect_krel() {
	[ -n "$KREL" ] && return 0
	[ -f "$MODULES_TAR" ] || die "$MODULES_TAR not found"
	# No head(1) in this pipeline: with pipefail its early exit would turn
	# tar's SIGPIPE into a fatal 141.
	KREL=$(tar -tzf "$MODULES_TAR" 2>/dev/null |
		sed -n 's|^\.*/lib/modules/\([^/]*\)/.*|\1|p')
	KREL=${KREL%%$'\n'*}
	[ -n "$KREL" ] || die "cannot detect the module version in $MODULES_TAR; use --kver"
}

check_inputs() {
	local f
	[ -f "$KERNEL_IMG" ] || die "kernel not found: $KERNEL_IMG
    Run scripts/build-kernel.sh first (or pass --kernel)."
	[ -f "$BOARD_DTB" ] || die "DTB not found: $BOARD_DTB
    Run scripts/build-kernel.sh first (or pass --dtb)."
	[ -f "$CMDLINE_FILE" ] || die "cmdline not found: $CMDLINE_FILE"
	[ -x "$VENDOR_TOOLS/ampack" ] || die "ampack not found: $VENDOR_TOOLS/ampack"
	[ -x "$VENDOR_TOOLS/make_ext4fs" ] || die "make_ext4fs not found: $VENDOR_TOOLS/make_ext4fs"
	need tar
	need python3
	need sha256sum

	for f in "${VENDOR_ITEMS[@]}"; do
		[ -f "$VENDOR_STAGING/$f" ] ||
			die "vendor asset missing: $VENDOR_STAGING/$f"
	done
	[ -f "$VENDOR_DTB_TEMPLATE" ] ||
		die "vendor DTB container missing: $VENDOR_DTB_TEMPLATE"

	if [ "$NO_ROOTFS" = 1 ]; then
		[ -f "$VENDOR_STAGING/system.PARTITION" ] ||
			die "--no-rootfs: $VENDOR_STAGING/system.PARTITION not found"
	else
		[ -f "$ROOTFS_TAR" ] || die "rootfs tarball not found: $ROOTFS_TAR"
		[ -f "$MODULES_TAR" ] || die "modules tarball not found: $MODULES_TAR"
		detect_krel
	fi
}

prepare_staging() {
	local staging_real vendor_real
	staging_real=$(realpath -m "$STAGING_DIR")
	vendor_real=$(realpath -m "$VENDOR_STAGING")
	[ "$staging_real" != "$vendor_real" ] ||
		die "refusing to rebuild the vendor staging directory in place: $VENDOR_STAGING"

	if [ -d "$STAGING_DIR" ] && [ -n "$(ls -A "$STAGING_DIR" 2>/dev/null)" ]; then
		[ -f "$STAGING_DIR$STAGING_OWNER_SUFFIX" ] ||
			die "$STAGING_DIR is not empty and was not created by this script.
    Remove it manually or pass --staging DIR."
		rm -rf "$STAGING_DIR"
	fi
	mkdir -p "$STAGING_DIR"
	printf 'AtriOS mainline test staging: safe to delete\n' > "$STAGING_DIR$STAGING_OWNER_SUFFIX"
}

link_or_copy() {
	local src=$1 dst=$2
	ln "$src" "$dst" 2>/dev/null || cp -p "$src" "$dst"
}

stage_vendor_assets() {
	local f
	for f in "${VENDOR_ITEMS[@]}"; do
		link_or_copy "$VENDOR_STAGING/$f" "$STAGING_DIR/$f"
	done
	log "staged vendor U-Boot/assets (${#VENDOR_ITEMS[@]} files)"
}

# The vendor BL33 does not take the kernel DTB from boot.PARTITION: with
# DTB_BIND_KERNEL undefined it runs "store dtb read ${dtb_mem_addr}" in
# board_late_init(), which reads the AML_ multi-DTB container from the "dtb"
# partition, and bootm then passes that blob to the kernel.  Leaving the
# vendor DTB there would give Linux 7.3 a 4.9 device tree.
#
# Reuse the vendor container header (its three 16-byte id strings are what
# checkhw() matches against the board) and swap in our DTB as the single
# payload.  The vendor container is version 2 with one entry; anything else
# is rejected instead of silently producing a half-valid image.
build_dtb_container() {
	local out="$STAGING_DIR/_aml_dtb.PARTITION"
	local n

	python3 - "$VENDOR_DTB_TEMPLATE" "$BOARD_DTB" "$out" <<'PY'
import struct
import sys

template_path, dtb_path, out_path = sys.argv[1:4]
vendor = open(template_path, "rb").read()
dtb = open(dtb_path, "rb").read()
PAGE = 2048

def fail(msg):
    print(f"[x] dtb container: {msg}", file=sys.stderr)
    sys.exit(1)

if vendor[:4] != b"AML_":
    fail(f"{template_path} is not an AML_ multi-DTB container")
version, total = struct.unpack("<II", vendor[4:12])
if version != 2:
    fail(f"unsupported multi-DTB version {version}")
if total != 1:
    fail(f"expected exactly one DTB in {template_path}, found {total}")
if vendor[0x800:0x804] != b"\xd0\x0d\xfe\xed":
    fail(f"{template_path} payload is not at offset 0x800")

hdr = bytearray(vendor[:PAGE])
struct.pack_into("<I", hdr, 0x3C, PAGE)                 # entry offset
aligned = (len(dtb) + PAGE - 1) // PAGE * PAGE
struct.pack_into("<I", hdr, 0x40, aligned)              # padded payload size
img = bytes(hdr) + dtb + b"\x00" * (aligned - len(dtb))
open(out_path, "wb").write(img)

check = open(out_path, "rb").read()
if check[:4] != b"AML_" or struct.unpack("<I", check[8:12])[0] != 1:
    fail("written container has a bad header")
if struct.unpack("<I", check[0x3C:0x40])[0] != PAGE:
    fail("written container has a bad entry offset")
if check[PAGE:PAGE + len(dtb)] != dtb:
    fail("written container payload differs from the input DTB")
print(f"    dtb container  {len(img)} B (header kept, payload {len(dtb)} B)")
PY

	for n in meson1.dtb meson1_ENC.dtb; do
		link_or_copy "$out" "$STAGING_DIR/$n"
	done
	log "rebuilt ${DTB_ASSETS[*]} from $(basename "$BOARD_DTB")"
}

# Build boot.PARTITION in the format the vendor U-Boot's imgread/bootm code
# expects.  Layout (all little endian):
#
#   0x000  "ANDROID!"
#   0x008  kernel_size, kernel_addr     (used by bootm_load_os)
#   0x010  ramdisk_size = 0, ramdisk_addr
#   0x018  second_size = DTB size, second_addr = 0x00f00000
#   0x020  tags_addr = 0x100, page_size = 2048
#   0x028  kernel_version (vendor sample: 1)
#   0x02c  os_version = 0
#   0x030  name[16] = zero
#   0x040  cmdline[512]
#   0x240  id[8] = zero
#   0x660  recovery_dtbo_size = 0, recovery_dtbo_offset = 0, header_size = 1648
#   0x800  kernel (Image.gz), padded to page_size
#         then DTB ("second stage"), padded to page_size
#
# Nothing outside Python's stdlib is used.
build_boot_partition() {
	local out=$1
	python3 - "$KERNEL_IMG" "$BOARD_DTB" "$CMDLINE_FILE" "$out" "$KERNEL_ADDR" <<'PY'
import gzip
import struct
import sys

MAGIC = b"ANDROID!"
PAGE = 2048
RAMDISK_ADDR = 0x01000000
SECOND_ADDR = 0x00F00000
TAGS_ADDR = 0x00000100
KERNEL_VERSION = 1          # byte at 0x28; the vendor sample has 1
OS_VERSION = 0
HEADER_SIZE = 1648
DTB_MAGIC = b"\xd0\x0d\xfe\xed"
GZIP_MAGIC = b"\x1f\x8b"
UBOOT_LOADADDR = 0x03080000     # bootloader "loadaddr" env
MOVE_THRESHOLD = 32 * 1024 * 1024  # android_image_need_move()
MIN_KIMG_ALIGN = 2 * 1024 * 1024
# reserved-memory from meson-sm1-yandex-station-max.dtb: secmon@5000000
# (0x300000) + secmon@5300000 (0x2000000).
BOARD_RESERVED = ((0x05000000, 0x07300000),)

kernel_path, dtb_path, cmdline_path, out_path = sys.argv[1:5]
KERNEL_ADDR = int(sys.argv[5], 0)

kernel = open(kernel_path, "rb").read()
dtb = open(dtb_path, "rb").read()
cmdline = open(cmdline_path, "r", encoding="utf-8").read().strip().encode()

def fail(msg):
    print(f"[x] boot.PARTITION: {msg}", file=sys.stderr)
    sys.exit(1)

if len(kernel) == 0:
    fail(f"kernel is empty: {kernel_path}")
if kernel[:2] != GZIP_MAGIC:
    fail(f"{kernel_path} is not gzip-compressed (magic {kernel[:2].hex()})")
if len(dtb) == 0:
    fail(f"DTB is empty: {dtb_path}")
if dtb[:4] != DTB_MAGIC:
    fail(f"{dtb_path} is not a flattened device tree (magic {dtb[:4].hex()})")
dtb_total = struct.unpack(">I", dtb[4:8])[0]
if dtb_total > len(dtb):
    fail(f"DTB totalsize {dtb_total} > file size {len(dtb)}")
if not cmdline:
    fail(f"empty cmdline: {cmdline_path}")
if len(cmdline) > 512:
    fail(f"cmdline is {len(cmdline)} bytes, the header only holds 512")

def pad(data):
    rem = len(data) % PAGE
    return data if rem == 0 else data + b"\x00" * (PAGE - rem)

# The image must sit at text_offset bytes from a 2 MB aligned base, and its
# decompressed size must not run into the boot image U-Boot keeps at
# loadaddr (android_image_need_move only helps when the two are <32 MB apart).
image = gzip.decompress(kernel)
text_off, image_size = struct.unpack("<QQ", image[8:24])
if image_size == 0:
    image_size = len(image)
if (KERNEL_ADDR - text_off) % MIN_KIMG_ALIGN:
    fail(f"kernel_addr 0x{KERNEL_ADDR:x} (minus text_offset 0x{text_off:x}) "
         f"is not 2 MB aligned")
total = PAGE + len(pad(kernel)) + len(pad(dtb))
src_end = UBOOT_LOADADDR + total
dst_end = KERNEL_ADDR + image_size
overlap = KERNEL_ADDR < src_end and UBOOT_LOADADDR < dst_end
if overlap and abs(KERNEL_ADDR - UBOOT_LOADADDR) >= MOVE_THRESHOLD:
    fail(f"kernel load range 0x{KERNEL_ADDR:x}..0x{dst_end:x} overlaps the "
         f"boot image at 0x{UBOOT_LOADADDR:x}..0x{src_end:x} and U-Boot will "
         f"not relocate it; choose another --kernel-addr")
for lo, hi in BOARD_RESERVED:
    if KERNEL_ADDR < hi and lo < dst_end:
        fail(f"kernel load range 0x{KERNEL_ADDR:x}..0x{dst_end:x} overlaps "
             f"reserved memory 0x{lo:x}..0x{hi:x}; choose another --kernel-addr")

hdr = bytearray(PAGE)
hdr[0:8] = MAGIC
struct.pack_into(
    "<10I", hdr, 8,
    len(kernel), KERNEL_ADDR,
    0, RAMDISK_ADDR,
    len(dtb), SECOND_ADDR,
    TAGS_ADDR, PAGE,
    KERNEL_VERSION, OS_VERSION,
)
hdr[48:64] = b"\x00" * 16
hdr[64:64 + len(cmdline)] = cmdline
# Android boot header v1 tail: recovery_dtbo_size, recovery_dtbo_offset,
# header_size.  The header itself stays one page.
struct.pack_into("<IQI", hdr, 1632, 0, 0, HEADER_SIZE)

img = bytes(hdr) + pad(kernel) + pad(dtb)
second_off = PAGE + len(pad(kernel))

with open(out_path, "wb") as f:
    f.write(img)

# Parse the file back the same way U-Boot's imgread does.
raw = open(out_path, "rb").read()
if raw[:8] != MAGIC:
    fail("written image has no ANDROID! magic")
fields = struct.unpack("<10I", raw[8:48])
ksize, kaddr, rsize, raddr, ssize, saddr, tags, page, kver, osver = fields
if ksize != len(kernel) or ssize != len(dtb):
    fail("size fields do not match the input files")
if raw[PAGE:PAGE + 2] != GZIP_MAGIC:
    fail("kernel payload is not at page 1")
if raw[second_off:second_off + 4] != DTB_MAGIC:
    fail("DTB payload is not at the expected second-stage offset")
if struct.unpack(">I", raw[second_off + 4:second_off + 8])[0] != dtb_total:
    fail("DTB totalsize changed while packing")
if len(raw) % PAGE:
    fail("image size is not page aligned")

print(f"    boot.PARTITION  {len(raw)} B")
print(f"      kernel        {len(kernel)} B gzip -> {len(image)} B at 0x{PAGE:x}")
print(f"      load range    0x{KERNEL_ADDR:x}..0x{dst_end:x} (boot image at 0x{UBOOT_LOADADDR:x})")
print(f"      dtb           {len(dtb)} B at 0x{second_off:x} (load 0x{saddr:x})")
print(f"      page_size     {page}  kernel_version {kver}  header_size {HEADER_SIZE}")
print(f"      cmdline       {len(cmdline)} B")
PY
}

build_rootfs() {
	local tree="$STAGING_DIR/.rootfs"
	local ko_count

	log "unpacking rootfs: $ROOTFS_TAR"
	rm -rf "$tree"
	mkdir -p "$tree"
	tar -xf "$ROOTFS_TAR" -C "$tree" --numeric-owner

	log "overlaying Linux $KREL modules from $MODULES_TAR"
	tar -xzf "$MODULES_TAR" -C "$tree" --numeric-owner
	[ -d "$tree/lib/modules/$KREL" ] ||
		die "modules were not installed into $tree/lib/modules/$KREL"
	ko_count=$(find "$tree/lib/modules/$KREL" -name '*.ko' | wc -l)
	[ "$ko_count" -gt 0 ] || die "no .ko files under lib/modules/$KREL"

	# Apply the mainline userspace overlay (asound.conf, S01modules for 7.3,
	# fb symlinks, adapted atrios helpers).
	if [ -d "$MAINLINE_DIR/overlay" ]; then
		log "applying mainline userspace overlay"
		cp -a "$MAINLINE_DIR/overlay/." "$tree"/
	fi

	# The vendor cmdline uses init=/init, which the Android rootfs provided.
	# The Buildroot tree only has /sbin/init, so add the symlink instead of
	# rewriting the vendor command line.
	if [ ! -e "$tree/init" ]; then
		ln -s sbin/init "$tree/init"
		log "added /init -> sbin/init (vendor cmdline uses init=/init)"
	fi

	cat > "$tree/etc/atrios-test-image" <<EOF
AtriOS Mainline
kernel:     Linux $KREL (mainline) + $(basename "$BOARD_DTB")
bootloader: $(if [ "${FINAL_IMAGE:-0}" = 1 ]; then \
                echo "mainline U-Boot v2026.10 (BL33 inside the vendor FIP)"; \
              else \
                echo "vendor U-Boot 2015.01 (BL33)"; \
              fi)
userspace:  Buildroot from $(basename "$ROOTFS_TAR") (built for 4.9.113)
hardware:   NOT TESTED
built:      $(date -u '+%Y-%m-%dT%H:%M:%SZ')
note:       the mainline overlay is applied; 7.3 modules load via
            /etc/init.d/S01modules, the 4.9 tree is kept only as a fallback.
EOF

	if command -v depmod >/dev/null 2>&1; then
		log "depmod -b <rootfs> $KREL"
		if ! depmod -b "$tree" "$KREL"; then
			warn "depmod failed; modules.dep from modules.tar is still present"
		fi
	else
		warn "depmod not found; using modules.dep from modules.tar"
	fi

	rm -f "$STAGING_DIR/system.PARTITION"

	# make_ext4fs has a canned Android fs_config: without -C it rewrites
	# /bin/* to 0755 gid 2000 and /etc/shadow to 0644.  Feed it the real
	# uid/gid/mode of every entry instead.
	local fsconfig="$STAGING_DIR/.fs_config"
	printf 'system 0 0 755\n' > "$fsconfig"
	( cd "$tree" && find . -mindepth 1 -printf 'system/%P %U %G %m\n' ) >> "$fsconfig"
	log "make_ext4fs -C <fs_config> -l $SYSTEM_SIZE -a system"
	"$VENDOR_TOOLS/make_ext4fs" -C "$fsconfig" -l "$SYSTEM_SIZE" -a system \
		"$STAGING_DIR/system.PARTITION" "$tree"
	rm -f "$fsconfig"

	# Check the filesystem without mounting it (no root required).
	local deb
	if command -v debugfs >/dev/null 2>&1; then
		log "checking the ext4 image with debugfs"
		debugfs -R "stat /lib/modules/$KREL/kernel/sound/soc/codecs/snd-soc-es7210.ko" \
			"$STAGING_DIR/system.PARTITION" 2>/dev/null | grep 'Inode:' >/dev/null ||
			die "es7210 module missing from system.PARTITION"
		debugfs -R "stat /lib/modules/4.9.113" \
			"$STAGING_DIR/system.PARTITION" 2>/dev/null | grep 'Inode:' >/dev/null ||
			die "the old 4.9.113 module tree disappeared"
		deb=$(debugfs -R "ls -l /lib/modules/$KREL/kernel/sound/soc/codecs" \
			"$STAGING_DIR/system.PARTITION" 2>/dev/null |
			grep -c 'snd-soc-\(es7210\|es8156\|sy6045s\)\.ko' || true)
		[ "$deb" -eq 3 ] || warn "expected 3 codec modules, debugfs listed $deb"
		debugfs -R "stat /bin/busybox" "$STAGING_DIR/system.PARTITION" \
			2>/dev/null | grep -E 'Mode: +04755' >/dev/null ||
			die "the setuid bit on /bin/busybox was lost (fs_config problem)"
		debugfs -R "stat /etc/shadow" "$STAGING_DIR/system.PARTITION" \
			2>/dev/null | grep -E 'Mode: +0600' >/dev/null ||
			die "/etc/shadow is not 0600 in system.PARTITION"
	else
		warn "debugfs not found; skipping the ext4 content check"
	fi

	rm -rf "$tree"
	log "system.PARTITION: $ko_count modules under /lib/modules/$KREL"
}

verify_boot_partition() {
	local boot=$1
	python3 - "$boot" "$KERNEL_IMG" "$BOARD_DTB" <<'PY'
import struct
import sys

boot_path, kernel_path, dtb_path = sys.argv[1:4]
raw = open(boot_path, "rb").read()
kernel = open(kernel_path, "rb").read()
dtb = open(dtb_path, "rb").read()

def fail(msg):
    print(f"[x] verify {boot_path}: {msg}", file=sys.stderr)
    sys.exit(1)

if raw[:8] != b"ANDROID!":
    fail("bad magic")
ksize, kaddr, rsize, raddr, ssize, saddr, tags, page, kver, osver = \
    struct.unpack("<10I", raw[8:48])
if page != 2048:
    fail(f"unexpected page_size {page}")
if ksize != len(kernel):
    fail(f"kernel_size {ksize} != {len(kernel)}")
if ssize != len(dtb):
    fail(f"second_size {ssize} != {len(dtb)}")
second_off = page + ((ksize + page - 1) // page) * page
if raw[second_off:second_off + ssize] != dtb:
    fail("second-stage bytes differ from the input DTB")
if raw[page:page + ksize] != kernel:
    fail("kernel bytes differ from the input Image.gz")
print(f"    boot.PARTITION parses: kernel {ksize} B, dtb {ssize} B at 0x{second_off:x}")
PY
}

print_summary() {
	local img=$1
	local f
	echo
	log "test image: $img"
	printf '    %-10s %s\n' "size" "$(stat -c %s "$img") B"
	printf '    %-10s %s\n' "sha256" "$(sha256_of "$img")"
	echo
	log "packed items"
	for f in "$STAGING_DIR"/*; do
		[ -f "$f" ] || continue
		printf '    %12s  %s\n' "$(stat -c %s "$f")" "$(basename "$f")"
	done
}

main() {
	check_inputs
	mkdir -p "$IMAGES_DIR"
	prepare_staging

	stage_vendor_assets
	build_dtb_container

	log "building boot.PARTITION from $KERNEL_IMG + $(basename "$BOARD_DTB")"
	build_boot_partition "$STAGING_DIR/boot.PARTITION"
	verify_boot_partition "$STAGING_DIR/boot.PARTITION"

	if [ "$NO_ROOTFS" = 1 ]; then
		log "--no-rootfs: reusing $VENDOR_STAGING/system.PARTITION"
		link_or_copy "$VENDOR_STAGING/system.PARTITION" \
			"$STAGING_DIR/system.PARTITION"
	else
		build_rootfs
	fi

	local img="$IMAGES_DIR/atrios-mainline-test-v$VERSION.img"
	log "packing with ampack (v2, verify)"
	"$VENDOR_TOOLS/ampack" pack --out-ver v2 --verify "$STAGING_DIR" "$img"

	log "ampack verify $img"
	"$VENDOR_TOOLS/ampack" verify "$img" >/dev/null

	( cd "$IMAGES_DIR" && sha256sum "$(basename "$img")" ) > "$img.sha256"

	print_summary "$img"
	echo
	warn "TEST image: vendor U-Boot 2015 (BL33) + mainline kernel; userspace is 4.9"
	warn "read docs/image.md before flashing - the vendor cmdline still says init=/init"
}

main "$@"
