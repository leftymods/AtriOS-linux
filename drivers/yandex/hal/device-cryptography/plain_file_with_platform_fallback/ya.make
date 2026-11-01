OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/platform_key
    yandex_io/libs/base
    yandex_io/libs/json_utils
    yandex_io/libs/cryptography
    yandex_io/libs/device_cryptography
)

SRCS(
    plain_file_with_platform_fallback.cpp
)

END()
