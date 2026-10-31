LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/ustimer/config
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/ustimer/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emdrv/common
    smart_devices/third_party/gecko_sdk/platform/emlib
)

SRCS(
    src/ustimer.c
)

END()
