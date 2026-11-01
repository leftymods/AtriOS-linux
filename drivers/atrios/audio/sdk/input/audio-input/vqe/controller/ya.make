OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/vqe/engine
    yandex_io/modules/audio_input/vqe/engine_factory
    yandex_io/libs/json_utils
)

SRCS(
    vqe_controller.cpp
)

END()

RECURSE(
    yandex_vqe
)
