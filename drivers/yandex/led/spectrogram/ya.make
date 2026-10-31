OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/ipc 
    yandex_io/libs/json_utils
    yandex_io/libs/protobuf_utils
    yandex_io/modules/leds/led_manager
    yandex_io/sdk
)

SRCS(
    filters.cpp
    spectrogram.cpp
    spectrogram_animation.cpp
)

END()
