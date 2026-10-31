LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/libs/base
    yandex_io/libs/telemetry
    yandex_io/modules/audio_input/audio_device/base
    yandex_io/modules/audio_input/vqe/controller
)

SRCS(
    yandex_vqe_audio_device_base.cpp
)

END()
