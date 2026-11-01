OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/pcba)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/libs/json_utils
    yandex_io/libs/telemetry/null
    library/cpp/testing/gmock_in_unittest
)

SRCS(
    test_pcba_ids.cpp
)

END()
