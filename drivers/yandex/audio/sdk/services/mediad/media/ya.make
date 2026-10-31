OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/interfaces/multiroom
    yandex_io/interfaces/stereo_pair
    yandex_io/libs/audio_player/base
    yandex_io/libs/base
    yandex_io/libs/equalizer_config
    yandex_io/libs/gogol
    yandex_io/libs/ipc
    yandex_io/sdk
    yandex_io/services/mediad/audio_clock_manager
    yandex_io/services/mediad/spectrum_listener
    yandex_io/services/mediad/media/players
    yandex_io/services/mediad/media/players/yandexmusic
    yandex_io/services/mediad/media/players/yandexradio2

    library/cpp/string_utils/url
    library/cpp/cgiparam
)

SRCS(
    media_endpoint.cpp
    players_controller.cpp
)

END()

RECURSE(
    players
)

RECURSE_FOR_TESTS(tests)
