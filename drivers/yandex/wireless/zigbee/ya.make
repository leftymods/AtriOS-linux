OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/zigbee/proto
    smart_devices/libs/zigbee/zcl

    yandex_io/libs/base
    yandex_io/libs/json_utils
)

SRCS(
    endpoint.cpp
    network.cpp
    node.cpp
    profiles.cpp
    utils.cpp
    zigbee.cpp
)

END()

RECURSE(
    ezsp
    ncp
    utils
    zcl
)

RECURSE_FOR_TESTS(
    ut
)
