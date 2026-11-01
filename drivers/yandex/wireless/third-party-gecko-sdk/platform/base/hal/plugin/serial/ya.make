LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/platform/emlib
)

NO_COMPILER_WARNINGS()

SRCS(
    cortexm/efm32/com.c
    ember-printf.c
    serial.c
)

END()
