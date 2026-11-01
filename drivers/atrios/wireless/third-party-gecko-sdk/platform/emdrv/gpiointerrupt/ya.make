LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/emdrv/gpiointerrupt/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emlib
)

IF (OS_NONE)
    SRCS(
        src/gpiointerrupt.c
    )
ENDIF()

END()
