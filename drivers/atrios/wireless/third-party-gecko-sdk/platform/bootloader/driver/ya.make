OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/bootloader/core
)

CFLAGS(
    -DBTL_DRIVER_UART_RX_BUFFER_SIZE=512
    -DBTL_DRIVER_UART_TX_BUFFER_SIZE=128
)

SRCS(
    btl_driver_delay.c
    btl_driver_euart.c
    btl_driver_uart.c
    btl_driver_util.c
)

END()
