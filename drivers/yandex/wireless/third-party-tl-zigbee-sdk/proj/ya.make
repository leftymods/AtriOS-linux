OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/proj
    smart_devices/third_party/tl_zigbee_sdk/zigbee/common/includes
)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/platform
)

SRCS(
    common/list.c
    common/mempool.c
    common/string.c
    GLOBAL common/tlPrintf.c

    drivers/drv_adc.c
    drivers/drv_flash.c
    drivers/drv_gpio.c
    drivers/drv_hw.c
    drivers/drv_i2c.c
    drivers/drv_keyboard.c
    drivers/drv_nv.c
    drivers/drv_pm.c
    drivers/drv_putchar.c
    drivers/drv_pwm.c
    drivers/drv_spi.c
    drivers/drv_timer.c
    drivers/drv_uart.c

    drivers/usb/usb.c
    drivers/usb/usbdesc.c

    drivers/usb/app/usbcdc.c
    drivers/usb/app/usbkb.c
    drivers/usb/app/usbmouse.c
    drivers/usb/app/usbvendor.c

    os/ev.c
    os/ev_buffer.c
    os/ev_poll.c
    os/ev_queue.c
    os/ev_timer.c
)

END()
