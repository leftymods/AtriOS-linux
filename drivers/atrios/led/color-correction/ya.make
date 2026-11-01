OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/logging
    yandex_io/libs/json_utils
)

SRCS(
    led_color_config.cpp
    utils.cpp
)

END()

RECURSE_FOR_TESTS(
    tests
)
