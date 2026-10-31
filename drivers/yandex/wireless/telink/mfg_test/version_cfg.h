#pragma once

#include <smart_devices/third_party/tl_zigbee_sdk/apps/common/comm_cfg.h>

#if defined(MCU_CORE_B91)
    #define CHIP_TYPE TLSR_9518
#endif

#define APP_RELEASE 0x10   // app release 1.0
#define APP_BUILD 0x01     // app build 01
#define STACK_RELEASE 0x30 // stack release 3.0
#define STACK_BUILD 0x01   // stack build 01

#define MANUFACTURER_CODE_TELINK 0x132F
#define IMAGE_TYPE ((CHIP_TYPE << 8) | IMAGE_TYPE_GW)
#define FILE_VERSION ((APP_RELEASE << 24) | (APP_BUILD << 16) | (STACK_RELEASE << 8) | STACK_BUILD)

#define IS_BOOT_LOADER_IMAGE 0
#define RESV_FOR_APP_RAM_CODE_SIZE 0
#define IMAGE_OFFSET APP_IMAGE_ADDR
