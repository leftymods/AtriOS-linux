LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/platform/emlib
)

SRCS(
    debug-jtag-efr32.c
)

END()
