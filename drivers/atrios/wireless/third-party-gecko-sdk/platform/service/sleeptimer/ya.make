LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/service/sleeptimer/config
    GLOBAL smart_devices/third_party/gecko_sdk/platform/service/sleeptimer/inc
    smart_devices/third_party/gecko_sdk/platform/service/power_manager/config
    smart_devices/third_party/gecko_sdk/platform/service/power_manager/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/common
    smart_devices/third_party/gecko_sdk/platform/emlib
)

IF (OS_NONE)
    SRCS(
        src/sl_sleeptimer.c
        src/sl_sleeptimer_hal_rtcc.c
    )
ENDIF()

END()
