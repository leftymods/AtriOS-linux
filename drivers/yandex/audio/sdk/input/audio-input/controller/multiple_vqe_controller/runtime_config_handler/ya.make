OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/controller/multiple_vqe_controller
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/sdk
    contrib/deprecated/jsoncpp
)

SRCS(
    active_vqe_config_handler.cpp
)

END()
