LIBRARY()

OWNER(
    akastornov
    g:yandex_io
)

CFLAGS(-Wno-unused-parameter)

PEERDIR(
    contrib/libs/webrtc
    yandex_io/libs/audio_player/gstreamer
    yandex_io/libs/audio_player/base
    yandex_io/libs/device
    yandex_io/libs/ipc/datacratic
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/sdk
    yandex_io/modules/audio_input/callback_listener
    yandex_io/interfaces/user_config
)

SRCS(
    quasar_audio_device_module.cpp
)

END()
