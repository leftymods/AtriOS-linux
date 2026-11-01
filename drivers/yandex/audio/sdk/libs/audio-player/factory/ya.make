OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/audio_player/base
    yandex_io/libs/audio_player/gstreamer
    yandex_io/libs/audio_player/mock
    yandex_io/libs/ipc
    yandex_io/libs/json_utils  # TODO: remove
    yandex_io/libs/base
    yandex_io/protos
)

SRCS(
    player_factory_selector.cpp
)

END()
