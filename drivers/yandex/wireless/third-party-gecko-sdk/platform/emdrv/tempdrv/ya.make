LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/tempdrv/config
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/tempdrv/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emdrv/common
    smart_devices/third_party/gecko_sdk/platform/emlib
)

SRCS(
    src/tempdrv.c
)

END()
