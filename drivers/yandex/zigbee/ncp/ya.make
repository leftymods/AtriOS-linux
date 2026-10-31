OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/zigbee
    smart_devices/libs/zigbee/ezsp

    yandex_io/libs/json_utils
)

SRCS(
    i_ncp.cpp
    i_ncp_factory.cpp
    ncp_configuration.cpp
    ncp_listener.cpp
)

END()

RECURSE(
    mock
    silabs
    telink
)
