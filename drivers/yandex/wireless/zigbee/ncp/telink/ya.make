OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/telink/hci
    smart_devices/libs/zigbee/ncp
)

SRCS(
    telink_ncp.cpp
)

END()
