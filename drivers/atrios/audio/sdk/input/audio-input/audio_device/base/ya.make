OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/libs/audio/common
    yandex_io/modules/audio_input/audio_device
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/libs/ring_buffer
    yandex_io/libs/threading
)

SRCS(
    audio_device_base.cpp
)

END()
