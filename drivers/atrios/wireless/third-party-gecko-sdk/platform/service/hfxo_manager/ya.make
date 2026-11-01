LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/service/hfxo_manager/config
    GLOBAL smart_devices/third_party/gecko_sdk/platform/service/hfxo_manager/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/common
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/platform/service/sleeptimer
)

IF (OS_NONE)
    SRCS(
        src/sl_hfxo_manager.c
        src/sl_hfxo_manager_hal_s2.c
    )
ENDIF()

END()
