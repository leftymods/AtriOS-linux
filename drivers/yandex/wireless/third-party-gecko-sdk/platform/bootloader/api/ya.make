OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/bootloader
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emlib
)

IF (OS_NONE)
    SRCS(
        btl_interface.c
        btl_interface_storage.c
    )
ENDIF()

END()
