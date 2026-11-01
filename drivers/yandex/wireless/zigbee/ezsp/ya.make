OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/libs/libev

    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/zigbee-framework

    yandex_io/libs/logging
    yandex_io/libs/threading
)

SRCS(
    callbacks.cpp
    ezsp.cpp
    ezsp_listener.cpp
)

END()
