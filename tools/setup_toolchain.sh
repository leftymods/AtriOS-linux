#!/bin/bash
set -e
mkdir -p /home/lmods/.local/bin
for tool in gcc cpp as ld objcopy objdump nm ar ranlib; do
    ln -sf /usr/bin/arm-linux-gnueabi-$tool /home/lmods/.local/bin/arm-none-eabi-$tool
    ln -sf /usr/bin/aarch64-linux-gnu-$tool /home/lmods/.local/bin/aarch64-none-elf-$tool
done
echo "Toolchain links created in /home/lmods/.local/bin"
