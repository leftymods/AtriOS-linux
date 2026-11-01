OWNER(g:yandex_io)

LIBRARY()

IF (OS_NONE)
    INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

    PEERDIR(
        smart_devices/third_party/tl_zigbee_sdk/proj
    )
ELSE()
    ADDINCL(
        smart_devices/libs/telink/bootloader/test
    )
ENDIF()

CFLAGS(
    GLOBAL -DBOOTLOADER_FLASH_2M
)

SRCS(
    bootloader.c
    crc.c
    uart.c
    xmodem.c
)

END()

IF (OS_NONE)
    RECURSE(
        bin
    )
ENDIF()

IF (OS_LINUX)
    RECURSE_FOR_TESTS(
        test
    )
ENDIF()
