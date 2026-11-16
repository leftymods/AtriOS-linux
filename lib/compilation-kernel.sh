#!/bin/bash
# ==============================================================================
# AtriOS Build System - Linux Kernel Compilation
# ==============================================================================

build_kernel() {
    display_alert "INFO" "Starting kernel compilation for" "${BOARD_NAME}"

    local kernel_dir="$PROJECT_ROOT/cache/kernel"

    prepare_kernel_source "$kernel_dir"

    local defconfig_src="$PROJECT_ROOT/$UBOOT_SOURCE_DIR/${KERNEL_DEFCONFIG}"
    local dts_src="$PROJECT_ROOT/$UBOOT_SOURCE_DIR/dts/${DTS_NAME}.dts"
    local out_dir="$PROJECT_ROOT/output/images"

    [ -s "$defconfig_src" ] || exit_with_error "Kernel defconfig missing: $defconfig_src"
    [ -s "$dts_src" ] || exit_with_error "Kernel DTS missing: $dts_src"
    export ARCH=arm64
    export CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

    # Stage current repository sources and build rules without deleting foreign files.
    # Replaced files are retained under kernel/.atrios-source-backups/.
    python3 "$PROJECT_ROOT/lib/kernel-tree.py" "$kernel_dir" \
        "$PROJECT_ROOT/drivers/atrios" "$dts_src" "$defconfig_src"

    # 2.5 Apply kernel patches if available
    local patches_dir="$PROJECT_ROOT/$UBOOT_SOURCE_DIR/patches/linux-7.0"
    if [ -d "$patches_dir" ]; then
        for patch_file in "$patches_dir"/*.patch; do
            if [ -f "$patch_file" ]; then
                display_alert "INFO" "Applying kernel patch:" "$(basename "$patch_file")"
                apply_kernel_patch "$kernel_dir" "$patch_file"
            fi
        done
    fi

    # 4. Build Kernel Image & DTBs
    local build_jobs="${BUILD_JOBS:-$(nproc)}"
    [[ "$build_jobs" =~ ^[1-9][0-9]*$ ]] || exit_with_error "Invalid BUILD_JOBS: $build_jobs"
    display_alert "INFO" "Compiling Linux kernel (Image & DTBs) using $build_jobs jobs..."
    (
        cd "$kernel_dir"
        make olddefconfig
        make -j"$build_jobs" Image dtbs modules
    )

    # 5. Copy built artifacts to output directory
    mkdir -p "$out_dir"
    local built_image="$kernel_dir/arch/arm64/boot/Image"
    local built_dtb="$kernel_dir/arch/arm64/boot/dts/amlogic/${DTS_NAME}.dtb"

    [ -f "$built_image" ] || exit_with_error "Kernel Image build failed: $built_image not found"
    
    mkdir -p "$(dirname "$KERNEL_IMAGE")" "$(dirname "$DTB_IMAGE")"
    cp "$built_image" "$KERNEL_IMAGE"
    display_alert "OK" "Kernel Image successfully compiled:" "$out_dir/Image"

    [ -s "$built_dtb" ] || exit_with_error "Kernel DTB build failed: $built_dtb not found"
    cp "$built_dtb" "$DTB_IMAGE"
    display_alert "OK" "Device Tree compiled:" "$DTB_IMAGE"
}

# Reject a patch that is neither cleanly applicable nor already applied.
apply_kernel_patch() {
    local kernel_dir="$1" patch_file="$2"
    if patch --batch --dry-run --forward -d "$kernel_dir" -p1 < "$patch_file" >/dev/null 2>&1; then
        patch --batch --forward -d "$kernel_dir" -p1 < "$patch_file"
    elif patch --batch --dry-run --reverse -d "$kernel_dir" -p1 < "$patch_file" >/dev/null 2>&1; then
        display_alert INFO "Patch already applied:" "$(basename "$patch_file")"
    else
        exit_with_error "Kernel patch is incompatible or partially applied: $patch_file"
    fi
}

# Preserve existing local trees. Only an absent cache uses the verified pin.
prepare_kernel_source() {
    local kernel_dir="$1"
    if [ -d "$kernel_dir" ]; then
        [ -f "$kernel_dir/Makefile" ] || exit_with_error "Invalid existing kernel cache: $kernel_dir"
        return
    fi
    if [ -e "$kernel_dir" ] || [ -L "$kernel_dir" ]; then
        exit_with_error "Refusing to replace an existing kernel cache path: $kernel_dir"
    fi
    mkdir -p "$(dirname "$kernel_dir")"
    if [ -n "${KERNEL_SOURCE_DIR:-}" ]; then
        [ -f "$KERNEL_SOURCE_DIR/Makefile" ] || exit_with_error "Invalid KERNEL_SOURCE_DIR: $KERNEL_SOURCE_DIR"
        ln -s "$(realpath "$KERNEL_SOURCE_DIR")" "$kernel_dir"
    else
        python3 "$PROJECT_ROOT/lib/kernel-source.py" \
            --url "${KERNEL_ARCHIVE_URL:-}" --sha256 "${KERNEL_ARCHIVE_SHA256:-}" \
            --destination "$kernel_dir"
    fi
}
