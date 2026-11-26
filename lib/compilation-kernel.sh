#!/bin/bash
# ==============================================================================
# AtriOS Build System - Linux Kernel Compilation
# ==============================================================================

build_kernel() {
    display_alert "INFO" "Starting kernel compilation for" "${BOARD_NAME}"

    resolve_kernel_build_profile --prepare
    local kernel_dir="$KERNEL_BUILD_DIR"
    local defconfig_src="$KERNEL_BUILD_CONFIG_PATH" dts_src="$KERNEL_BUILD_DTS_PATH"
    local out_dir="$PROJECT_ROOT/output/images"
    local ARCH="$KERNEL_BUILD_ARCH"
    export ARCH
    export CROSS_COMPILE="${CROSS_COMPILE-}"

    # This tree belongs to one board, source identity, config, drivers and patch set.
    python3 "$PROJECT_ROOT/lib/kernel-tree.py" "$kernel_dir" \
        "$KERNEL_BUILD_DRIVERS_DIR" "$dts_src" "$defconfig_src" \
        --arch "$KERNEL_BUILD_ARCH" --dts-subdir "$KERNEL_BUILD_DTS_SUBDIR" --dtb-config "$KERNEL_BUILD_DTB_CONFIG" || \
        exit_with_error "Kernel source integration failed for $BOARD"

    # 2.5 Apply kernel patches if available
    local patches_dir="$KERNEL_BUILD_PATCH_DIR"
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
    display_alert "INFO" "Compiling Linux kernel ($KERNEL_BUILD_IMAGE_TARGET & DTBs) using $build_jobs jobs..."
    if ! (
        cd "$kernel_dir" &&
        make olddefconfig &&
        make -j"$build_jobs" "$KERNEL_BUILD_IMAGE_TARGET" dtbs modules
    ); then
        exit_with_error "Kernel build failed for selected profile: $kernel_dir"
    fi

    # 5. Copy built artifacts to output directory
    mkdir -p "$out_dir"
    local built_image="$kernel_dir/arch/$KERNEL_BUILD_ARCH/boot/$KERNEL_BUILD_IMAGE_TARGET"
    local built_dtb="$kernel_dir/arch/$KERNEL_BUILD_ARCH/boot/dts/${KERNEL_BUILD_DTS_SUBDIR:+$KERNEL_BUILD_DTS_SUBDIR/}${DTS_NAME}.dtb"

    [ -s "$built_image" ] || exit_with_error "Kernel Image build failed: $built_image not found"
    
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

# Legacy source-preparation API, retained for callers that only need a source tree.
# Production builds and packages use resolve_kernel_build_profile instead.
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

# Safe before or after a build: resolution hashes inputs but does not fetch/mutate
# sources unless --prepare is passed. Values are read as data, never shell code.
resolve_kernel_build_profile() {
    local profile_data dts_subdir_unset=1
    local -a fields
    [ "${KERNEL_DTS_SUBDIR+x}" != x ] || dts_subdir_unset=0
    profile_data=$(BOARD="${BOARD:-}" ARCH="${ARCH:-}" KERNEL_ARCH="${KERNEL_ARCH:-}" \
        BOARDFAMILY="${BOARDFAMILY:-}" CROSS_COMPILE="${CROSS_COMPILE:-}" \
        UBOOT_SOURCE_DIR="${UBOOT_SOURCE_DIR:-}" KERNEL_DEFCONFIG="${KERNEL_DEFCONFIG:-}" \
        DTS_NAME="${DTS_NAME:-}" KERNEL_SOURCE_DIR="${KERNEL_SOURCE_DIR:-}" \
        KERNEL_ARCHIVE_URL="${KERNEL_ARCHIVE_URL:-}" KERNEL_ARCHIVE_SHA256="${KERNEL_ARCHIVE_SHA256:-}" \
        KERNEL_CONFIG_PATH="${KERNEL_CONFIG_PATH:-}" KERNEL_DTS_PATH="${KERNEL_DTS_PATH:-}" \
        KERNEL_PATCH_DIR="${KERNEL_PATCH_DIR:-}" KERNEL_DRIVERS_DIR="${KERNEL_DRIVERS_DIR:-}" \
        KERNEL_DTS_SUBDIR="${KERNEL_DTS_SUBDIR-}" KERNEL_DTS_SUBDIR_UNSET="$dts_subdir_unset" \
        KERNEL_DTB_CONFIG="${KERNEL_DTB_CONFIG:-}" KERNEL_IMAGE_TARGET="${KERNEL_IMAGE_TARGET:-}" \
        python3 "$PROJECT_ROOT/lib/kernel-profile.py" --project-root "$PROJECT_ROOT" "$@") || \
        exit_with_error "Could not resolve the selected kernel build profile"
    mapfile -t fields <<< "$profile_data"
    [ "${#fields[@]}" -eq 10 ] || exit_with_error "Invalid kernel profile result"
    KERNEL_BUILD_DIR="${fields[0]}"
    KERNEL_BUILD_ARCH="${fields[1]}"
    KERNEL_BUILD_DTS_SUBDIR="${fields[2]}"
    KERNEL_BUILD_DTB_CONFIG="${fields[3]}"
    KERNEL_BUILD_IMAGE_TARGET="${fields[4]}"
    KERNEL_BUILD_CONFIG_PATH="${fields[5]}"
    KERNEL_BUILD_DTS_PATH="${fields[6]}"
    KERNEL_BUILD_PATCH_DIR="${fields[7]}"
    KERNEL_BUILD_DRIVERS_DIR="${fields[8]}"
    KERNEL_PROFILE_FINGERPRINT="${fields[9]}"
    export KERNEL_BUILD_DIR KERNEL_BUILD_ARCH KERNEL_BUILD_DTS_SUBDIR KERNEL_BUILD_DTB_CONFIG KERNEL_BUILD_IMAGE_TARGET
    export KERNEL_BUILD_CONFIG_PATH KERNEL_BUILD_DTS_PATH KERNEL_BUILD_PATCH_DIR KERNEL_BUILD_DRIVERS_DIR KERNEL_PROFILE_FINGERPRINT
}
