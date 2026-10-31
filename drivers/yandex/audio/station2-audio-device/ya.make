LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/deprecated/jsoncpp
    voicetech/vqe/libs/downsampling # downsampler
    yandex_io/libs/audio/reader
    yandex_io/libs/audio/tinyalsa
    yandex_io/libs/base
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/libs/telemetry
    yandex_io/modules/audio_input/audio_device/stats
    yandex_io/modules/audio_input/audio_device/yandex_vqe/base
    yandex_io/modules/audio_input/channels_splitter
    yandex_io/modules/audio_input/interleaver
    yandex_io/modules/audio_input/vqe/controller
    yandex_io/modules/audio_input/vqe/engine_factory
    yandex_io/modules/audio_input/vqe/feedback_shift_metrics
)

SRCS(
    audio_readers.cpp
    rcu_audio_device.cpp
    tinyalsa_mixer.cpp
    yandex_station2_audio_device.cpp
)

END()

RECURSE_FOR_TESTS(
    tests
)
