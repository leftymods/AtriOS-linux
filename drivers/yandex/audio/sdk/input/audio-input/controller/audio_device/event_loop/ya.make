LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    yandex_io/modules/audio_input/controller/audio_device/utils
    yandex_io/modules/audio_input/controller

    yandex_io/sdk

    yandex_io/modules/audio_input/audio_device
    yandex_io/libs/base
    yandex_io/libs/logging

)

SRCS(
    event_loop_audio_device_input_controller.cpp
)

END()
