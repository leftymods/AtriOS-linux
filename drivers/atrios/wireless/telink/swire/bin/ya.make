OWNER(g:yandex_io)

PROGRAM(swire)

PEERDIR(
    smart_devices/libs/telink/swire

    yandex_io/libs/logging
    yandex_io/libs/logging/setup

    library/cpp/getopt/small
)

SRCS(
    main.cpp
)

END()
