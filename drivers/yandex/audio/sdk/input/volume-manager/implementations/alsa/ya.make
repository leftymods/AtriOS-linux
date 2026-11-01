OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/audio/alsa
    yandex_io/modules/volume_manager/base
)

SRCS(
    alsa_volume_manager.cpp
)

END()
