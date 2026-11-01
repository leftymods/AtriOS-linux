LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/deprecated/jsoncpp
    voicetech/vqe/libs/downsampling
    yandex_io/libs/audio/alsa
    yandex_io/libs/audio/reader
    yandex_io/libs/base
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/libs/telemetry
    yandex_io/modules/audio_input/audio_device/base
    yandex_io/modules/audio_input/audio_device/stats
    yandex_io/modules/audio_input/audio_device/yandex_vqe/base
    yandex_io/modules/audio_input/vqe/controller/yandex_vqe
    yandex_io/modules/audio_input/vqe/engine/yandex_c
)

SRCS(
    yandex_vqe_audio_device.cpp
)

END()

RECURSE(
    base
)
