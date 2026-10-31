OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/sdk
    yandex_io/libs/audio_player/base
    yandex_io/libs/base
    yandex_io/libs/ipc
    yandex_io/libs/logging 
)

SRCS(
    spectrum_listener.cpp
    spectrum_provider.cpp
)

END()
