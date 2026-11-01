OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/json_utils
    yandex_io/libs/base
    yandex_io/libs/logging
    yandex_io/libs/telemetry
    yandex_io/libs/threading
)

SRCS(
    pcba_ids.cpp
)

END()

RECURSE_FOR_TESTS(tests)
