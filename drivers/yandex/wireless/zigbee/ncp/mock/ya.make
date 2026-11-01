OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    library/cpp/testing/gmock_in_unittest
    smart_devices/libs/zigbee/ncp
)

SRCS(
    mock_ncp.cpp
)

END()
