OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/controller
    yandex_io/modules/audio_input/controller/audio_device/utils
    yandex_io/modules/audio_input/controller/multiple_vqe_controller
    yandex_io/modules/audio_input/vqe/controller
    yandex_io/sdk
    yandex_io/modules/audio_input/audio_device
    yandex_io/libs/base
    yandex_io/libs/json_utils
    yandex_io/libs/logging
)

SRCS(
    vqe_controller_audio_input_controller.cpp
)

END()
