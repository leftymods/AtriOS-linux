LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/spidrv/config
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/spidrv/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emdrv/dmadrv
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/platform/service/power_manager
    smart_devices/third_party/gecko_sdk/platform/service/sleeptimer
)

SRCS(
    src/spidrv.c
)

END()
