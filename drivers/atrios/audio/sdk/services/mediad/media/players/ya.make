OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/interfaces/multiroom
    yandex_io/libs/base
    yandex_io/libs/device
    yandex_io/libs/equalizer_config
    yandex_io/libs/ete_metrics
    yandex_io/libs/http_client
    yandex_io/libs/json_utils
    yandex_io/protos
)

SRCS(
    player.cpp
    player_factory.cpp
    player_type.cpp
    volume_smoother.cpp
)

END()

RECURSE(
    yandexmusic
    yandexradio2
)
