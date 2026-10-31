LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/dmadrv/config
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/dmadrv/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emdrv/common
    smart_devices/third_party/gecko_sdk/platform/emlib
)

IF (OS_NONE)
    SRCS(
        src/dmadrv.c
    )
ENDIF()

END()
