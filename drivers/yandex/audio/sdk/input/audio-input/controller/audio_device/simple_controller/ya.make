OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/logging
    yandex_io/modules/audio_input/audio_device
    yandex_io/modules/audio_input/controller
    yandex_io/modules/audio_input/controller/audio_device/utils
    yandex_io/modules/audio_input/vqe/controller
    yandex_io/sdk
)

SRCS(
    simple_audio_device_input_controller.cpp
)

END()
