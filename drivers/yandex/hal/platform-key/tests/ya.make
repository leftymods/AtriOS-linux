OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/platform_key)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/libs/cryptography
)

SRCS(
    test_platform_key.cpp
)

END()
