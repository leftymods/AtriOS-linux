OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/services/sound_initd)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/libs/http_client
    yandex_io/libs/json_utils
    yandex_io/libs/ipc
    yandex_io/libs/setup_parser
    yandex_io/libs/base
    yandex_io/interfaces/device_state/mock
    yandex_io/services/sound_initd
)

SRCS(
    test_sound_init.cpp
)

DATA(
    arcadia/yandex_io/services/sound_initd/tests/test_data
)

END()
