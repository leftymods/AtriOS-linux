OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/capabilities/file_player
    yandex_io/libs/base
    yandex_io/libs/device
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/services/mediad/spectrum_listener
    yandex_io/services/mediad/media/players
)

SRCS(
    yandex_radio_player2.cpp
)

END()
