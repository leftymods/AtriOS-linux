OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/logging
    yandex_io/libs/terminate_waiter
    yandex_io/modules/leds/led_controller
    yandex_io/modules/leds/led_manager
    smart_devices/libs/spectrogram
)

SRCS(
    sc_led_blinking_clock_animation.cpp
    sc_led_clock_animation.cpp
    sc_led_controller.cpp
    sc_led_fading_clock_animation.cpp
    sc_led_frame.cpp
    sc_led_storage.cpp
    sc_led_pattern.cpp
    sc_led_timer_countdown_animation.cpp
    sc_led_volume_animation.cpp
    sc_led_util.cpp
    spectrogram/sc_led_spectrogram.cpp
    spectrogram/sc_led_spectrogram_animation.cpp
)

END()

RECURSE(
    tools
)
