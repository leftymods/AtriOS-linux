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

    library/cpp/string_utils/url
    library/cpp/cgiparam
)

SRCS(
    audio_client_utils.cpp
    audio_client_controller.cpp
    audio_client.cpp
    audio_client_endpoint.cpp
    stream_app_src.cpp
)

END()

RECURSE_FOR_TESTS(tests)
