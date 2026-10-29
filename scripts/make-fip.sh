#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build the Amlogic FIP (BL2 + BL30 + BL31 + BL33) with the mainline U-Boot
# as BL33, so the image no longer needs the vendor 2015 U-Boot.
#
#   scripts/make-fip.sh [options]
#
# Format (AML FIP v3, as produced by aml_encrypt_g12a --bootmk):
#
#   0x00000  bl2.n.bin.sig (0x10000 B, starts with a 16-byte build salt,
#            then the "@AML" FIP header at +0x10 and the signed BL2)
#   0x10000  component table: 4 entries of {16-byte tag, u64 off, u64 size}
#   0x20000  DDR firmware blobs (byte-identical to the vendor FIP)
#   ...      BL30 (entry 0), BL31 (entry 1), BL32 empty (entry 2),
#            BL33 (entry 3, lz4 container produced by --compress lz4)
#
# The vendor FIP v112 (atrios-uboot2015-2019-lz4-v112/u-boot.bin) was built
# from the signed BL2/BL30/BL31 intermediates in /home/leftymods/fip-tmp-v112
# (bl2.n.bin.sig, bl30_new.bin.enc, bl31.img.enc).  Re-running
# aml_encrypt_g12a --bootmk with those exact intermediates and a new
# bl33.bin.enc reproduces the vendor image byte-for-byte except for BL33 and
# the per-build encryption salt: BL2 stays identical, BL30/BL31 keep their
# vendor plaintext and signatures.  This is the default mode.
#
# If the stage directory is not available, the script refuses to build a
# release FIP (losing the vendor BL2 also loses the board-specific DDR
# training).  --no-stage explicitly rebuilds BL2/BL30/BL31 from the generic
# SM1 payloads in amlogic-boot-fip/khadas-vim3l; that is for experiments
# only -- see docs/final-image.md.
#
# Environment:
#   OUT_DIR        artifact root (default out)
#   FIP_BL33       BL33 payload (default $OUT_DIR/uboot/u-boot.bin)
#   FIP_OUT_DIR    output directory (default $OUT_DIR/fip)
#   FIP_STAGE_DIR  pre-signed BL2/BL30/BL31 intermediates
#                  (default /home/leftymods/fip-tmp-v112, "-" disables)
#   FIP_SRC        amlogic-boot-fip board dir (default khadas-vim3l)
#   FIP_REFERENCE  vendor FIP used for the sanity check
#                  (default /home/leftymods/atrios-uboot2015-2019-lz4-v112/u-boot.bin)
#   FIP_MAX_SIZE   burn limit in bytes (default 2097152, 2 MiB)
set -euo pipefail

# shellcheck source=lib.sh
# shellcheck source-path=SCRIPTDIR
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

FIP_SRC="${FIP_SRC:-/home/leftymods/amlogic-boot-fip/khadas-vim3l}"
FIP_BL33="${FIP_BL33:-$OUT_DIR/uboot/u-boot.bin}"
FIP_OUT_DIR="${FIP_OUT_DIR:-$OUT_DIR/fip}"
FIP_STAGE_DIR="${FIP_STAGE_DIR:-/home/leftymods/fip-tmp-v112}"
FIP_REFERENCE="${FIP_REFERENCE:-/home/leftymods/atrios-uboot2015-2019-lz4-v112/u-boot.bin}"
FIP_MAX_SIZE="${FIP_MAX_SIZE:-2097152}"

LZ4=1
BURN=0
DDRFW_COUNT=8
VERIFY_ONLY=""
ALLOW_NO_STAGE=0

# The eight DDR firmware blobs the vendor v112 FIP carries.  Adding
# lpddr3_1d.fw (the ninth file in the board dir) grows the image by 0x10000
# and no longer matches the vendor layout, so it is opt-in.
DDRFW_FILES=(
	ddr4_1d.fw
	ddr4_2d.fw
	ddr3_1d.fw
	piei.fw
	lpddr4_1d.fw
	lpddr4_2d.fw
	diag_lpddr4.fw
	aml_ddr.fw
)

# The pre-signed vendor BL2/BL30/BL31 inputs from the v112 build.
stage_usable() {
	[ "$FIP_STAGE_DIR" != "-" ] && [ -d "$FIP_STAGE_DIR" ] &&
		[ -f "$FIP_STAGE_DIR/bl2.n.bin.sig" ] &&
		[ -f "$FIP_STAGE_DIR/bl30_new.bin.enc" ] &&
		[ -f "$FIP_STAGE_DIR/bl31.img.enc" ]
}

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Build the Amlogic FIP with mainline U-Boot as BL33 (AML FIP v3).

Options:
      --bl33 FILE     BL33 payload (default $FIP_BL33)
      --out DIR       output directory (default $FIP_OUT_DIR)
      --src DIR       amlogic-boot-fip board directory (default $FIP_SRC)
      --stage DIR     pre-signed BL2/BL30/BL31 intermediates
                      (default $FIP_STAGE_DIR, "-" = rebuild from raw)
      --no-stage      rebuild BL2/BL30/BL31 from $FIP_SRC raw payloads
                      (experimental; loses the vendor DDR training)
      --reference FIP vendor FIP for the layout sanity check
                      (default $FIP_REFERENCE, "-" disables)
      --no-lz4        store BL33 uncompressed (usually > 2 MiB, not usable)
      --burn          use $OUT_DIR/uboot/burn/u-boot.bin when it exists;
                      this is the hook for the mainline USB-burn build
      --ddrfw N       number of DDR firmware blobs, 4..8 (default $DDRFW_COUNT)
      --verify FIP    only verify an existing FIP against --bl33 and exit
  -h, --help          this help

Environment:
  FIP_BL33=$FIP_BL33
  FIP_OUT_DIR=$FIP_OUT_DIR
  FIP_SRC=$FIP_SRC
  FIP_STAGE_DIR=$FIP_STAGE_DIR
  FIP_REFERENCE=$FIP_REFERENCE
  FIP_MAX_SIZE=$FIP_MAX_SIZE

Artifacts:
  <out>/u-boot.bin            FIP (bootloader.PARTITION / UBOOT.USB / DDR.USB)
  <out>/u-boot.bin.sd.bin     FIP with the 512-byte SD/MMC prefix
  <out>/u-boot.bin.usb.bl2    BootROM BL2 stage
  <out>/u-boot.bin.usb.tpl    USB Burning Tool template
  <out>/u-boot.bin.sha256
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--bl33)
			[ -n "${2:-}" ] || die "--bl33 requires a file"
			FIP_BL33=$2; shift 2 ;;
		--out)
			[ -n "${2:-}" ] || die "--out requires a directory"
			FIP_OUT_DIR=$2; shift 2 ;;
		--src)
			[ -n "${2:-}" ] || die "--src requires a directory"
			FIP_SRC=$2; shift 2 ;;
		--stage)
			[ -n "${2:-}" ] || die "--stage requires a directory or -"
			FIP_STAGE_DIR=$2; shift 2 ;;
		--no-stage)
			FIP_STAGE_DIR="-"
			ALLOW_NO_STAGE=1
			shift ;;
		--reference)
			[ -n "${2:-}" ] || die "--reference requires a file or -"
			FIP_REFERENCE=$2; shift 2 ;;
		--no-lz4) LZ4=0; shift ;;
		--burn) BURN=1; shift ;;
		--ddrfw)
			[ -n "${2:-}" ] || die "--ddrfw requires a number"
			DDRFW_COUNT=$2; shift 2 ;;
		--verify)
			[ -n "${2:-}" ] || die "--verify requires a file"
			VERIFY_ONLY=$2; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
done

case "$DDRFW_COUNT" in
	4|5|6|7|8) ;;
	*) die "--ddrfw must be between 4 and 8 (got $DDRFW_COUNT)" ;;
esac

# --- burn hook -------------------------------------------------------------
# The mainline U-Boot in out/uboot already carries the Amlogic USB-burn
# protocol (CONFIG_AML_USB_BURN).  When the burn port is rebuilt separately,
# it is dropped into out/uboot/burn/u-boot.bin and --burn picks it up without
# touching this script.
if [ "$BURN" = 1 ]; then
	if [ -f "$OUT_DIR/uboot/burn/u-boot.bin" ]; then
		FIP_BL33="$OUT_DIR/uboot/burn/u-boot.bin"
		log "burn hook: using $FIP_BL33"
	elif [ -n "${FIP_BURN_BL33:-}" ] && [ -f "$FIP_BURN_BL33" ]; then
		FIP_BL33="$FIP_BURN_BL33"
		log "burn hook: using $FIP_BL33"
	else
		warn "burn hook: no out/uboot/burn/u-boot.bin, falling back to $FIP_BL33"
		warn "burn hook: the default build already has CONFIG_AML_USB_BURN if enabled"
	fi
fi

check_inputs() {
	local f
	[ -d "$FIP_SRC" ] || die "FIP source directory not found: $FIP_SRC"
	[ -f "$FIP_BL33" ] || die "BL33 not found: $FIP_BL33
    Run scripts/build-uboot.sh first (or pass --bl33)."
	[ -s "$FIP_BL33" ] || die "BL33 is empty: $FIP_BL33"

	[ -f "$FIP_SRC/aml_encrypt_g12a" ] ||
		die "missing $FIP_SRC/aml_encrypt_g12a"
	[ -x "$FIP_SRC/aml_encrypt_g12a" ] ||
		die "$FIP_SRC/aml_encrypt_g12a is not executable"

	if stage_usable; then
		log "BL2/BL30/BL31: using the vendor v112 intermediates in $FIP_STAGE_DIR"
	else
		[ "$ALLOW_NO_STAGE" = 1 ] || die "vendor BL2/BL30/BL31 stage not found: $FIP_STAGE_DIR
    A release FIP must keep the vendor BL2 (board DDR training).
    Restore the v112 intermediates or pass --no-stage explicitly."
		warn "stage dir unavailable: $FIP_STAGE_DIR"
		warn "rebuilding BL2/BL30/BL31 from $FIP_SRC raw payloads"
		for f in bl2.bin acs.bin bl30.bin bl301.bin bl31.img; do
			[ -f "$FIP_SRC/$f" ] || die "missing $FIP_SRC/$f"
		done
	fi

	local i
	for ((i = 0; i < DDRFW_COUNT; i++)); do
		[ -f "$FIP_SRC/${DDRFW_FILES[i]}" ] ||
			die "missing DDR firmware: $FIP_SRC/${DDRFW_FILES[i]}"
	done

	need python3
	need sha256sum
	need stat
}

# pad_to SRC LIMIT DST: copy SRC and extend it with zeroes up to LIMIT.
pad_to() {
	local src=$1 limit=$2 dst=$3 size
	size=$(stat -c %s "$src")
	[ "$size" -le "$limit" ] ||
		die "$(basename "$src") is $size B, limit is $limit B"
	cp "$src" "$dst"
	truncate -s "$limit" "$dst"
}

build_components() {
	local work=$1
	local ae="$FIP_SRC/aml_encrypt_g12a"

	if stage_usable; then
		log "using vendor BL2/BL30/BL31 from $FIP_STAGE_DIR"
		cp -f "$FIP_STAGE_DIR/bl2.n.bin.sig" "$work/bl2.n.bin.sig"
		cp -f "$FIP_STAGE_DIR/bl30_new.bin.enc" "$work/bl30_new.bin.enc"
		cp -f "$FIP_STAGE_DIR/bl31.img.enc" "$work/bl31.img.enc"
	else
		log "BL2: $(basename "$FIP_SRC")/bl2.bin + acs.bin (0xE000 + 0x1000)"
		pad_to "$FIP_SRC/bl2.bin" 57344 "$work/bl2_zero.bin"
		pad_to "$FIP_SRC/acs.bin" 4096 "$work/bl21_zero.bin"
		cat "$work/bl2_zero.bin" "$work/bl21_zero.bin" > "$work/bl2_new.bin"

		log "BL30: bl30.bin + bl301.bin (0xA000 + 0x3400)"
		pad_to "$FIP_SRC/bl30.bin" 40960 "$work/bl30_zero.bin"
		pad_to "$FIP_SRC/bl301.bin" 13312 "$work/bl301_zero.bin"
		cat "$work/bl30_zero.bin" "$work/bl301_zero.bin" > "$work/bl30_new.bin"

		log "signing BL30 (bl30sig + bl3sig v3)"
		"$ae" --bl30sig --input "$work/bl30_new.bin" \
			--output "$work/bl30_new.bin.g12.enc" --level v3
		"$ae" --bl3sig --input "$work/bl30_new.bin.g12.enc" \
			--output "$work/bl30_new.bin.enc" --level v3 --type bl30

		log "signing BL31 (bl3sig v3)"
		"$ae" --bl3sig --input "$FIP_SRC/bl31.img" \
			--output "$work/bl31.img.enc" --level v3 --type bl31

		log "signing BL2 (bl2sig)"
		"$ae" --bl2sig --input "$work/bl2_new.bin" \
			--output "$work/bl2.n.bin.sig"
	fi

	if [ "$LZ4" = 1 ]; then
		log "signing BL33 $(basename "$FIP_BL33") (bl3sig v3, lz4)"
		"$ae" --bl3sig --input "$FIP_BL33" \
			--output "$work/bl33.bin.enc" --level v3 --type bl33 \
			--compress lz4
	else
		log "signing BL33 $(basename "$FIP_BL33") (bl3sig v3, no compression)"
		"$ae" --bl3sig --input "$FIP_BL33" \
			--output "$work/bl33.bin.enc" --level v3 --type bl33
	fi
}

bootmk() {
	local work=$1 out=$2
	local ae="$FIP_SRC/aml_encrypt_g12a"
	local args=() i

	# bootmk cannot create the output directory itself (it fails with
	# "bootmk fail"), so it must exist before the call.
	mkdir -p "$(dirname "$out")"

	for ((i = 0; i < DDRFW_COUNT; i++)); do
		args+=("--ddrfw$((i + 1))" "$FIP_SRC/${DDRFW_FILES[i]}")
	done

	log "aml_encrypt_g12a --bootmk (FIP v3, $DDRFW_COUNT DDR firmware blobs)"
	"$ae" --bootmk --output "$out" --level v3 \
		--bl2 "$work/bl2.n.bin.sig" \
		--bl30 "$work/bl30_new.bin.enc" \
		--bl31 "$work/bl31.img.enc" \
		--bl33 "$work/bl33.bin.enc" \
		"${args[@]}"
}

verify_fip() {
	local fip=$1 bl33=$2 reference=$3 expect_bl2=${4:-0}
	python3 - "$fip" "$bl33" "$FIP_MAX_SIZE" "$reference" "$expect_bl2" <<'PY'
import hashlib
import os
import struct
import sys

fip_path, bl33_path, max_size, reference, expect_bl2 = sys.argv[1:6]
max_size = int(max_size)
raw = open(fip_path, "rb").read()
bl33 = open(bl33_path, "rb").read()

def fail(msg):
    print(f"[x] verify {fip_path}: {msg}", file=sys.stderr)
    sys.exit(1)

if len(raw) > max_size:
    fail(f"size {len(raw)} B exceeds the burn limit {max_size} B")
if len(raw) < 0x20000:
    fail("file is too small to be a FIP")

# --- top header -----------------------------------------------------------
if raw[0x10:0x14] != b"@AML":
    fail(f"no @AML magic at 0x10 (got {raw[0x10:0x14]!r})")
hdr = struct.unpack("<16I", raw[0x10:0x50])
if hdr[1] != 0x0000FFF0:
    fail(f"unexpected header size field 0x{hdr[1]:x}")
if hdr[2] != 0x01010040:
    fail(f"unexpected header flags 0x{hdr[2]:x}")

# --- component table ------------------------------------------------------
# 4 entries, each {16-byte tag, u64 offset, u64 size}; the last entry ends
# exactly at EOF.  Entry 0 = BL30, 1 = BL31, 2 = BL32 (empty), 3 = BL33.
toc = []
for i in range(4):
    base = 0x10020 + i * 0x28
    tag = raw[base:base + 16]
    off, size = struct.unpack("<QQ", raw[base + 16:base + 32])
    toc.append((off, size))
    if i and toc[i - 1][0] + toc[i - 1][1] != off:
        fail(f"TOC entry {i - 1} does not end at entry {i} "
             f"(0x{toc[i-1][0] + toc[i-1][1]:x} != 0x{off:x})")
last_off, last_size = toc[3]
# The table accounts for everything except the final 0x10000-byte block
# (the BL33 container is stored 0x10010 bytes past its TOC offset, and the
# last 0x10000 bytes are outside the table in every vendor build we checked).
if last_off + last_size != len(raw) - 0x10000:
    fail(f"last TOC entry ends at 0x{last_off + last_size:x}, "
         f"expected 0x{len(raw) - 0x10000:x} (file 0x{len(raw):x} - 0x10000)")
if toc[0][0] < 0x20000:
    fail(f"first component offset 0x{toc[0][0]:x} overlaps the header")

# --- BL33 lz4 container ---------------------------------------------------
# The last "@AML" v1 header in the file is the BL33 lz4 container.  It is
# stored at TOC[3].off + 0x10010; the raw lz4 block starts 0x300 bytes into
# the container (0x290-byte header + 0x70-byte metadata block).
magic = b"@AML\x01\x00\x00\x00"
pos = raw.rfind(magic)
if pos < 0:
    fail("no lz4 @AML container found (was --no-lz4 used?)")
expect = toc[3][0] + 0x10010
if pos != expect:
    fail(f"BL33 container at 0x{pos:x}, expected 0x{expect:x}")
comp, hdr_size = struct.unpack("<QQ", raw[pos + 0x10:pos + 0x20])
if hdr_size < 0x40 or hdr_size > 0x1000:
    fail(f"implausible lz4 header size 0x{hdr_size:x}")
if comp == 0 or comp > 0x200000:
    fail(f"implausible lz4 payload size 0x{comp:x}")

payload = raw[pos + 0x300:pos + 0x300 + comp]

def lz4_block_decompress(src, expected):
    out = bytearray()
    i = 0
    n = len(src)
    while i < n and len(out) < expected:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        if i + lit > n:
            raise ValueError("literal overruns the compressed buffer")
        out += src[i:i + lit]
        i += lit
        if i >= n or len(out) >= expected:
            break
        if i + 2 > n:
            raise ValueError("offset overruns the compressed buffer")
        off = src[i] | (src[i + 1] << 8)
        i += 2
        ml = token & 15
        if ml == 15:
            while True:
                b = src[i]
                i += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        start = len(out) - off
        if start < 0:
            raise ValueError(f"invalid back-reference at output {len(out)}")
        for k in range(ml):
            out.append(out[start + k])
    return bytes(out), i

try:
    out, used = lz4_block_decompress(payload, len(bl33))
except Exception as exc:
    fail(f"lz4 decompression failed: {exc}")
if len(out) != len(bl33):
    fail(f"decompressed BL33 is {len(out)} B, input is {len(bl33)} B")
if out != bl33:
    fail("decompressed BL33 differs from the input u-boot.bin")
if used > comp:
    fail("lz4 stream overruns the declared payload size")

print(f"    FIP             {len(raw)} B ({len(raw) / 1048576:.2f} MiB)")
print(f"    header          @AML v3, {len(raw)} B <= {max_size} B")
print(f"    TOC             BL30 {toc[0][1]:#x} B, BL31 {toc[1][1]:#x} B, "
      f"BL32 {toc[2][1]:#x} B, BL33 {toc[3][1]:#x} B")
print(f"    BL33 lz4        container @{pos:#x}, {comp} B -> {len(out)} B, "
      f"sha256 {hashlib.sha256(out).hexdigest()[:16]} (matches input)")
if used < comp:
    print(f"    BL33 lz4        {comp - used} B trailing padding in the container")

# --- optional vendor reference sanity check -------------------------------
if reference and reference != "-" and os.path.isfile(reference):
    ref = open(reference, "rb").read()
    if ref[0x10:0x14] != b"@AML" or len(ref) < 0x20000:
        print(f"    reference       {reference} is not a FIP, skipped")
    else:
        rtoc = []
        for i in range(4):
            base = 0x10020 + i * 0x28
            off, size = struct.unpack("<QQ", ref[base + 16:base + 32])
            rtoc.append((off, size))
        same = (rtoc[0][1] == toc[0][1] and rtoc[1][1] == toc[1][1])
        print(f"    reference       {os.path.basename(reference)} "
              f"({len(ref)} B), BL30/BL31 sizes "
              f"{'match' if same else 'DIFFER'}")
        if not same:
            print("    reference       WARNING: BL30/BL31 differ from the "
                  "vendor image; the boot chain changed more than BL33")
        # bootmk embeds bl2.n.bin.sig verbatim, so when the vendor stage is
        # used the first 0x10000 bytes must be identical to the reference.
        bl2_same = raw[:0x10000] == ref[:0x10000]
        if expect_bl2 == "1" and not bl2_same:
            fail("BL2 differs from the vendor reference, but the vendor "
                 "stage was requested")
        print(f"    reference       BL2 {'identical' if bl2_same else 'differs'} "
              f"({'vendor' if bl2_same else 'rebuilt'})")
PY
}

print_summary() {
	local fip=$1
	echo
	log "FIP: $fip"
	printf '    %-14s %s\n' "size" "$(stat -c %s "$fip") B"
	printf '    %-14s %s\n' "sha256" "$(sha256_of "$fip")"
	printf '    %-14s %s\n' "BL33" "$FIP_BL33"
	printf '    %-14s %s\n' "compression" "$([ "$LZ4" = 1 ] && echo lz4 || echo none)"
	echo
	log "artifacts"
	local f
	for f in "$FIP_OUT_DIR"/u-boot.bin "$FIP_OUT_DIR"/u-boot.bin.sd.bin \
		"$FIP_OUT_DIR"/u-boot.bin.usb.bl2 "$FIP_OUT_DIR"/u-boot.bin.usb.tpl; do
		[ -f "$f" ] || continue
		printf '    %12s  %s\n' "$(stat -c %s "$f")" "$(basename "$f")"
	done
}

main() {
	if [ -n "$VERIFY_ONLY" ]; then
		[ -f "$VERIFY_ONLY" ] || die "FIP not found: $VERIFY_ONLY"
		[ -f "$FIP_BL33" ] || die "BL33 not found: $FIP_BL33"
		need python3
		log "verifying existing FIP: $VERIFY_ONLY"
		verify_fip "$VERIFY_ONLY" "$FIP_BL33" "$FIP_REFERENCE" 0
		return 0
	fi

	check_inputs

	if [ "$BURN" = 1 ] && [ -f "$OUT_DIR/uboot/build/u-boot" ] &&
		command -v nm >/dev/null 2>&1; then
		if nm "$OUT_DIR/uboot/build/u-boot" 2>/dev/null |
			grep ' aml_usb_burn_run$' >/dev/null; then
			log "BL33 ELF contains aml_usb_burn_run (burn port compiled in)"
		else
			warn "BL33 ELF has no aml_usb_burn_run: burn mode is not available"
		fi
	fi

	mkdir -p "$FIP_OUT_DIR"
	local work="$FIP_OUT_DIR/.work"
	# The work directory only holds a few MiB; it is safe to wipe.
	rm -rf "$work"
	mkdir -p "$work"

	build_components "$work"
	bootmk "$work" "$FIP_OUT_DIR/u-boot.bin"

	local expect_bl2=0
	stage_usable && expect_bl2=1
	verify_fip "$FIP_OUT_DIR/u-boot.bin" "$FIP_BL33" "$FIP_REFERENCE" \
		"$expect_bl2"

	( cd "$FIP_OUT_DIR" && sha256sum u-boot.bin ) > "$FIP_OUT_DIR/u-boot.bin.sha256"
	rm -rf "$work"

	print_summary "$FIP_OUT_DIR/u-boot.bin"
	echo
	if stage_usable; then
		log "BL2/BL30/BL31: vendor v112 intermediates ($FIP_STAGE_DIR)"
		log "only BL33 was replaced: $(sha256_of "$FIP_BL33")"
		warn "read docs/final-image.md before flashing."
	else
		warn "BL2/BL30/BL31 rebuilt from $(basename "$FIP_SRC") generic SM1 payloads;"
		warn "only BL33 is guaranteed to differ from the vendor image."
		warn "Read docs/final-image.md before flashing."
	fi
}

main "$@"
