LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    yandex_io/libs/net
    yandex_io/libs/base
    yandex_io/libs/triggers
    yandex_io/sdk
)

SRCS(
    wifi_reseter.cpp
    settings.cpp
)

END()

RECURSE_FOR_TESTS(tests)
