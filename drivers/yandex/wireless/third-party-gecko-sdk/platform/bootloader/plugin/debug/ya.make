OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/bootloader/core
    smart_devices/third_party/gecko_sdk/platform/emlib
)

IF (SL_BOOTLOADER_PLUGIN_DEBUG_PRINT)
    CFLAGS(GLOBAL -DBTL_PLUGIN_DEBUG_PRINT)
ENDIF()

IF (SL_BOOTLOADER_PLUGIN_DEBUG_ASSERT)
    CFLAGS(GLOBAL -DBTL_PLUGIN_DEBUG_ASSERT)
ENDIF()

SRCS(
    btl_debug.c
)

END()
