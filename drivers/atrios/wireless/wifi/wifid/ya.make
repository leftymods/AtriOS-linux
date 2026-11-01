OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/restricted/boost
    yandex_io/interfaces/device_state
    yandex_io/interfaces/user_config
    yandex_io/libs/base
    yandex_io/libs/net
    yandex_io/libs/device
    yandex_io/libs/delay_timings_policy
    yandex_io/libs/http_client
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/threading
    yandex_io/libs/protobuf_utils
    yandex_io/libs/wpa_cli
    yandex_io/protos/enum_names
    yandex_io/protos
)

SRCS(
    http_head.cpp
    internet_checker.cpp
    wifi_endpoint.cpp
    wifi_manager.cpp
    wifi_utils.cpp
)

END()

RECURSE_FOR_TESTS(tests)
