LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    smart_devices/platforms/yandexstation_2/platformd/audio_device
    yandex_io/libs/base
    yandex_io/libs/device
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/modules/audio_input/audio_device/base
    yandex_io/modules/audio_input/audio_device/tcp
    yandex_io/modules/audio_input/audio_device/websocket
    yandex_io/modules/audio_input/controller/audio_device/utils
    yandex_io/modules/audio_input/controller/multiple_vqe_controller
    yandex_io/modules/audio_input/vqe/controller/yandex_vqe
    yandex_io/sdk
)

SRCS(
    audio_input_controller.cpp
)

END()

