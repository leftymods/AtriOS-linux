LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/protocol/zigbee/app/xncp
)

SRCS(
    ../../build/em260-xncp-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/em260-xncp-library.a
)

END()
