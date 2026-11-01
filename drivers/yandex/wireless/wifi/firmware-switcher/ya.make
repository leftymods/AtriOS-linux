LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/restricted/boost
    yandex_io/libs/logging
)

SRCS(
    firmware_switcher.cpp
)

END()
