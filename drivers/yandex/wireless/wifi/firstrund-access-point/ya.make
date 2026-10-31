OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/interfaces/auth
    yandex_io/interfaces/device_state
    yandex_io/interfaces/updates
    yandex_io/interfaces/user_config
    yandex_io/libs/base
    yandex_io/libs/cryptography
    yandex_io/libs/device
    yandex_io/libs/http_client
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/jwt
    yandex_io/libs/logging
    yandex_io/libs/threading
    yandex_io/protos/enum_names
    yandex_io/sdk
    yandex_io/libs/protobuf_utils
    library/cpp/http/server
    library/cpp/http/misc
)

SRCS(
    access_point.cpp
    check_token.cpp
    first_run_endpoint.cpp
    first_run_service.cpp
)

END()

RECURSE_FOR_TESTS(tests)
