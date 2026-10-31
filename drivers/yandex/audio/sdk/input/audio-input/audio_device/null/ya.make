OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/audio_device
    yandex_io/modules/audio_input/audio_device/base
)

SRCS(
    null_audio_device.cpp
)

END()
