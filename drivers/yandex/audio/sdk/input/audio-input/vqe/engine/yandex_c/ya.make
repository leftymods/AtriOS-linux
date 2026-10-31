OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/vqe/engine
    yandex_io/libs/audio/common
    yandex_io/libs/audio/reader
    yandex_io/libs/base
    yandex_io/libs/telemetry
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    voicetech/library/channel_types
    voicetech/vqe/yandex_vqe/c
)

SRCS(
    yandex_vqe_c_engine.cpp
)

END()
