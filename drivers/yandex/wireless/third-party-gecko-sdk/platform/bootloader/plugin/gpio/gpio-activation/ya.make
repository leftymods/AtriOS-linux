OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/bootloader/core
)

CFLAGS(
    -DBTL_GPIO_ACTIVATION_POLARITY=LOW
)

SRCS(
    btl_gpio_activation.c
)

END()
