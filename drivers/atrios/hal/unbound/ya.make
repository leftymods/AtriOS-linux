OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/json_utils
    yandex_io/libs/net
    yandex_io/libs/base
    yandex_io/libs/logging
    yandex_io/libs/telemetry
    yandex_io/libs/threading
    yandex_io/sdk
)

SRCS(
    unbound_enabler.cpp
)

END()

RECURSE_FOR_TESTS(tests)
