OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/libs/net
    yandex_io/sdk
    yandex_io/sdk/interfaces
    smart_devices/libs/iot/capability/interface
)

SRCS(
    iot_capability.cpp
    iot_capability_switcher.cpp
)

END()

RECURSE(interface)

RECURSE_FOR_TESTS(tests)
