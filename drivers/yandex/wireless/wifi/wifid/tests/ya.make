OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/services/wifid)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/interfaces/device_state/mock
    yandex_io/interfaces/user_config/mock
    yandex_io/libs/ipc
    yandex_io/libs/base
    yandex_io/libs/logging
    yandex_io/services/wifid/testlib
)

SRCS(
    test_wifi_endpoint.cpp
)

END()
