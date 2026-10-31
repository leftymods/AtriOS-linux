OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    contrib/restricted/alsa-lib
    yandex_io/libs/audio/alsa
    yandex_io/libs/audio/reader
    yandex_io/modules/audio_input/audio_device
    yandex_io/modules/audio_input/audio_device/base
    yandex_io/libs/base
    yandex_io/libs/errno
)

SRCS(
    alsa_audio_device.cpp
)

END()
