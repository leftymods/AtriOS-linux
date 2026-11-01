OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/zigbee/ncp
    smart_devices/libs/zigbee/ezsp

    yandex_io/libs/base
    yandex_io/libs/logging
)

SRCS(
    silabs_ncp.cpp
)

END()
