OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/interfaces/multiroom
    yandex_io/libs/audio_player/base
    yandex_io/libs/json_utils
    yandex_io/libs/websocket
    yandex_io/libs/base
    yandex_io/libs/self_destroyer
    yandex_io/libs/ipc
    yandex_io/services/mediad/spectrum_listener
    yandex_io/services/mediad/media/players
    yandex_io/interfaces/user_config/connector
)

SRCS(
    mp_listener.cpp
    yandex_music.cpp
    yandex_music_player.cpp
)

END()

RECURSE_FOR_TESTS(tests)
