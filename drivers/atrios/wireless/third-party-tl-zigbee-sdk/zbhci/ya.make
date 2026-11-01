OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zbhci
)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/proj
)

IF (TL_ZBHCI_CUSTOM)
    PEERDIR(
        smart_devices/third_party/tl_zigbee_sdk/zigbee/common/zb_task_queue
    )
ELSE()
    PEERDIR(
        smart_devices/third_party/tl_zigbee_sdk/zigbee
    )
ENDIF()

SRCS(
    uart/hci_uart.c

    usbCDC/zbhci_usb_cdc.c

    usbHID/zbhci_usb_hid.c

    usbPrint/hci_usbPrint.c

    zbhci.c
)

IF (NOT TL_ZBHCI_CUSTOM)
    SRCS(
        zbhci_zclHandler.c
        zbhciCmdProcess.c
    )
ENDIF()

END()
