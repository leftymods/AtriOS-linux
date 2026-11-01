LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/service/mpu/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/common
    smart_devices/third_party/gecko_sdk/platform/emlib
)

SRCS(
    src/sl_mpu.c
)

END()
