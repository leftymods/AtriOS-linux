OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/bootloader/core
    smart_devices/third_party/gecko_sdk/platform/bootloader/driver
)

CFLAGS(
    -DBTL_XMODEM_IDLE_TIMEOUT=0
)

SRCS(
    btl_comm_xmodem.c
)

END()
