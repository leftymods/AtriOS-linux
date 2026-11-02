#!/bin/bash
# ==============================================================================
# AtriOS Build System - Linux Kernel Compilation
# ==============================================================================

build_kernel() {
    display_alert "INFO" "Starting kernel compilation for" "${BOARD_NAME}"

    local kernel_dir="$PROJECT_ROOT/cache/kernel"

    # 1. Resolve Kernel Source in cache/kernel or Clone from Git
    if [ ! -d "$kernel_dir" ]; then
        if [ -d "/home/lmods/linux-7.3.1" ]; then
            display_alert "INFO" "Linking existing local kernel tree into" "$kernel_dir"
            mkdir -p "$PROJECT_ROOT/cache"
            ln -sfn "/home/lmods/linux-7.3.1" "$kernel_dir"
        else
            local git_url="${KERNEL_GIT_URL:-https://github.com/torvalds/linux.git}"
            local git_branch="${KERNEL_BRANCH:-master}"
            
            display_alert "WARN" "Kernel not found at $kernel_dir. Cloning from Git..."
            display_alert "INFO" "Repository:" "$git_url (branch: $git_branch)"
            
            mkdir -p "$PROJECT_ROOT/cache"
            git clone --depth 1 -b "$git_branch" "$git_url" "$kernel_dir"
            display_alert "OK" "Kernel cloned successfully into" "$kernel_dir"
        fi
    fi

    local defconfig_src="$PROJECT_ROOT/$UBOOT_SOURCE_DIR/${KERNEL_DEFCONFIG}"
    local dts_src="$PROJECT_ROOT/$UBOOT_SOURCE_DIR/dts/${DTS_NAME}.dts"
    local out_dir="$PROJECT_ROOT/output/images"

    export ARCH=arm64
    export CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

    # 2. Inject DTS into kernel tree if provided by board
    if [ -f "$dts_src" ]; then
        display_alert "INFO" "Injecting Device Tree:" "${DTS_NAME}.dts"
        mkdir -p "$kernel_dir/arch/arm64/boot/dts/amlogic"
        cp "$dts_src" "$kernel_dir/arch/arm64/boot/dts/amlogic/"
    fi

    # 3. Configure kernel
    if [ -f "$defconfig_src" ]; then
        display_alert "INFO" "Applying kernel defconfig:" "${KERNEL_DEFCONFIG}"
        cp "$defconfig_src" "$kernel_dir/.config"
    fi

    # 4. Build Kernel Image & DTBs
    display_alert "INFO" "Compiling Linux kernel (Image & DTBs) using $(nproc) jobs..."
    (
        cd "$kernel_dir"
        make olddefconfig
        make -j"$(nproc)" Image dtbs modules
    )

    # 5. Copy built artifacts to output directory
    mkdir -p "$out_dir"
    local built_image="$kernel_dir/arch/arm64/boot/Image"
    local built_dtb="$kernel_dir/arch/arm64/boot/dts/amlogic/${DTS_NAME}.dtb"

    [ -f "$built_image" ] || exit_with_error "Kernel Image build failed: $built_image not found"
    
    cp "$built_image" "$out_dir/Image"
    display_alert "OK" "Kernel Image successfully compiled:" "$out_dir/Image"

    if [ -f "$built_dtb" ]; then
        cp "$built_dtb" "$out_dir/${DTS_NAME}.dtb"
        display_alert "OK" "Device Tree compiled:" "$out_dir/${DTS_NAME}.dtb"
    fi
}
