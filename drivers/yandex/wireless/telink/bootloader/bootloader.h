#pragma once

#include "tl_common.h"

#define BOOTLOADER_APP_ADDRESS FLASH_ADDR_OF_APP_FW

#ifdef BOOTLOADER_FLASH_2M
    #define BOOTLOADER_OTA_ADDRESS 0x100000
    #define BOOTLOADER_OTA_MAX_SIZE (NV_BASE_ADDRESS - FLASH_ADDR_OF_APP_FW)
#else
    #define BOOTLOADER_OTA_ADDRESS FLASH_ADDR_OF_OTA_IMAGE
    #define BOOTLOADER_OTA_MAX_SIZE FLASH_OTA_IMAGE_MAX_SIZE
#endif

#if defined(__cplusplus)
extern "C" {
#endif

    typedef void (*BootloaderCallback)();

    void tlBootloaderMain(bool bootToApp, BootloaderCallback run, const bool* stop);

#if defined(__cplusplus)
}
#endif
