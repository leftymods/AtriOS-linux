OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/unbound)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    smart_devices/libs/unbound
    yandex_io/sdk
)

SRCS(
    test_unbound_enabler.cpp
)

END()
