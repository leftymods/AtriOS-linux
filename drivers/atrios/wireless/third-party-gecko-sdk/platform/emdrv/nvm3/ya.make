LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/nvm3/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emdrv/common
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/platform/service/mpu
)

CFLAGS(
    -DNVM3_FLASH_PAGES=4
    -DNVM3_DEFAULT_NVM_SIZE=NVM3_FLASH_PAGES*FLASH_PAGE_SIZE
    -DNVM3_DEFAULT_CACHE_SIZE=200
    -DNVM3_DEFAULT_MAX_OBJECT_SIZE=254
    -DNVM3_DEFAULT_REPACK_HEADROOM=0
)

SRCS(
    src/nvm3_default.c
    src/nvm3_hal_flash.c
    src/nvm3_lock.c

    lib/libnvm3_CM33_gcc.a
)

END()
