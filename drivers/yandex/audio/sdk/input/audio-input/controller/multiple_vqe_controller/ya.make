OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/modules/audio_input/controller
)

SRCS(
    i_multiple_vqe_controller.cpp
)

END()

RECURSE(
    runtime_config_handler
)
