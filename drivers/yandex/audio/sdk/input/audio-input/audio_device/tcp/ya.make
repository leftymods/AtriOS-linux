OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    contrib/libs/asio
    yandex_io/libs/base
    yandex_io/libs/audio/common
    yandex_io/modules/audio_input/audio_device
    yandex_io/modules/audio_input/audio_device/base
)

SRCS(
    tcp_audio_device.cpp
)

END()
