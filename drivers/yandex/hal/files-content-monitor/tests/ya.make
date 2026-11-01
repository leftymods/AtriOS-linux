OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/files_content_monitor)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/libs/base
    library/cpp/testing/gmock_in_unittest
)

SRCS(
    test_files_content_monitor.cpp
)

END()
