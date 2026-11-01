OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/capabilities/file_player
    yandex_io/interfaces/auth
    yandex_io/interfaces/multiroom
    yandex_io/interfaces/stereo_pair
    yandex_io/libs/audio_player/base
    yandex_io/libs/base
    yandex_io/libs/cryptography
    yandex_io/libs/device
    yandex_io/libs/equalizer_config
    yandex_io/libs/ete_metrics
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/signals
    yandex_io/libs/threading
    yandex_io/libs/websocket
    yandex_io/protos
    yandex_io/protos/enum_names
    yandex_io/sdk
    yandex_io/services/mediad/audioclient
    yandex_io/services/mediad/media
)

SRCS(
    media_service.cpp
)

END()

RECURSE(
    audioclient
    audio_clock_manager
    media
    spectrum_listener
)
