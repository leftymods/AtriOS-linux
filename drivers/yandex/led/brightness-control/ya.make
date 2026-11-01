OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/errno
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/libs/threading
)

SRCS(
    ambient_light_estimate.cpp
    brightness_animator.cpp
    brightness_config.cpp
    brightness_control.cpp
    i_light_sensor.cpp
    light_sensor.cpp
    light_sensor_hysteresis_reader.cpp
)

END()

RECURSE_FOR_TESTS(
    tests
)
