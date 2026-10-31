OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/brightness_control)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/tests/testlib
)

SRCS(
    test_brightness_config.cpp
)

END()
