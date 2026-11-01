OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/gpio
    smart_devices/libs/tty

    yandex_io/libs/base

    library/cpp/getopt/small
)

SRCS(
    gbl.cpp
    reader.cpp
)

END()
