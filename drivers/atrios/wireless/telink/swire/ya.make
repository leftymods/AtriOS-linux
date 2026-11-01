OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/tty

    yandex_io/libs/base
    yandex_io/libs/logging
)

SRCS(
    b91.cpp
    b91_flasher.cpp
    i_swire.cpp
    uart_swire.cpp
)

RESOURCE(
    data/dut_9518_flash_v0002.bin dut_9518_flash_v0002.bin
)

END()

RECURSE(
    bin
)
