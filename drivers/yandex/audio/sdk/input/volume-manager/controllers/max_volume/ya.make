OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/interfaces/user_config
    yandex_io/libs/json_utils
    yandex_io/modules/volume_manager/base
)

SRCS(
    max_volume_controller.cpp
)

END()
