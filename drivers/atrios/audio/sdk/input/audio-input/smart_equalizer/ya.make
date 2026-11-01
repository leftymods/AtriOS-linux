OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    voicetech/vqe/yandex_vqe/c
    yandex_io/libs/equalizer_config
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/sdk
)

SRCS(
    smart_equalizer.cpp
)

END()
