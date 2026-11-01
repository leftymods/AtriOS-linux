OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/dns)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/json_utils
    yandex_io/libs/telemetry/null
    yandex_io/tests/testlib/null_sdk

    library/cpp/testing/gmock_in_unittest
)

SRCS(
    test_dns_health_checker.cpp
    test_runtime_dns_config.cpp
)

END()
