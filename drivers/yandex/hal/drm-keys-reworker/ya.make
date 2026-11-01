LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/device

    yandex_io/interfaces/auth

    yandex_io/libs/http_client
    yandex_io/libs/json_utils
    yandex_io/libs/logging
)

SRCS(
    encryption_key_reworker.cpp
)

END()
