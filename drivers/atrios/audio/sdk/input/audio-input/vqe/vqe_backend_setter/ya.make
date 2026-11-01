OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/sdk
    yandex_io/modules/audio_input/controller/audio_device/utils
    yandex_io/modules/audio_input/vqe/controller
    yandex_io/modules/audio_input/vqe/engine_factory
)

SRCS(
    vqe_backend_setter.cpp
)

END()
