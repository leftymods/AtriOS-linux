LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/common/inc
    smart_devices/third_party/gecko_sdk/platform/common/config
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emlib
)

IF (OS_NONE)
    SRCS(
        src/sl_slist.c
        src/sl_status.c
        src/sl_string.c
    )
ENDIF()

END()
