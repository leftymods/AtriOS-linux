OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/protobuf_utils
    smart_devices/platforms/yandexstation_2/platformd/leds
)

SRCS(
    foreground_activity.cpp
    foreground_app_animation.cpp
    app/snake_foreground_app.cpp
    app/base/foreground_app.cpp
)

END()
