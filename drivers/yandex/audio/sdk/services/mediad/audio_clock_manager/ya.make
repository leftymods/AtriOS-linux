OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/audio_player/base
    yandex_io/libs/audio_player/gstreamer
    yandex_io/libs/base
    yandex_io/libs/logging 
    yandex_io/libs/json_utils
)

SRCS(
    audio_clock_manager.cpp
)

END()
