OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/capabilities/file_player
    yandex_io/modules/volume_manager/base
    yandex_io/sdk

    yandex_io/libs/json_utils
    yandex_io/libs/logging
)

SRCS(
    configuration_mode_handler.cpp
)

END()


