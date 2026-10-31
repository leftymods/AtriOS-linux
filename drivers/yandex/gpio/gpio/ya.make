LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    yandex_io/libs/errno
    yandex_io/libs/logging
)

SRCS(
    button_input.cpp
    gpio.cpp
    gpio_listener.cpp
    gpio_loop.cpp
)

END()
