OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/audio/alsa
    yandex_io/libs/audio_player/base
    yandex_io/libs/base
    yandex_io/libs/errno
)

SRCS(
    alsa_audio_player.cpp
    wav_decoder.cpp
)

END()
