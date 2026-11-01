OWNER(g:yandex_io)

UNITTEST()

ADDINCL(
    smart_devices/libs/telink/bootloader/test
)

PEERDIR(
    smart_devices/libs/gecko_bootloader
    smart_devices/libs/telink/bootloader
    smart_devices/libs/tty

    yandex_io/libs/logging

    contrib/libs/libev
)

SRCS(
    fake_clock.cpp
    fake_debug.cpp
    fake_flash.cpp
    fake_sys.cpp
    fake_uart.cpp

    test_bootloader.cpp
    test_uart.cpp
    test_xmodem.cpp
)

DATA(
    arcadia/smart_devices/libs/telink/bootloader/test/data/sample.ybl
)

LDFLAGS(-lutil)

END()
