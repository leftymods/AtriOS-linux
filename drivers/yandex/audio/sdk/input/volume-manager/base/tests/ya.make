OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/modules/volume_manager/base)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

REQUIREMENTS(ram_disk:1)

PEERDIR(
    library/cpp/testing/gmock_in_unittest
    yandex_io/tests/testlib/null_sdk
    yandex_io/libs/base
    yandex_io/libs/threading
    yandex_io/libs/telemetry/null
    yandex_io/libs/logging
    yandex_io/sdk
)

SRCS(
    test_volume_manager.cpp
)

END()
