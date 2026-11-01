OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    speechkit/core
    yandex_io/libs/base
    yandex_io/libs/logging
    yandex_io/libs/telemetry
    yandex_io/libs/websocket
    yandex_io/modules/audio_input/audio_device/base
    yandex_io/sdk
)

SRCS(
    websocket_audio_config_handler.cpp
    websocket_audio_device.cpp
)

END()
