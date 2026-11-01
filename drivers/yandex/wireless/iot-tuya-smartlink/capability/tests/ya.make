OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/iot/capability)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/sdk
    yandex_io/libs/iot/null
    yandex_io/tests/testlib/null_sdk
    library/cpp/testing/gmock_in_unittest
)

SRCS(
    test_iot_capability.cpp
)

END()
