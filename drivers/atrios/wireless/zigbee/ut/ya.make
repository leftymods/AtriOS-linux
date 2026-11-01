OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/libs/zigbee)

PEERDIR(
    library/cpp/testing/gmock_in_unittest
)

SRCS(
    test_utils.cpp
    test_zigbee.cpp
)

END()
