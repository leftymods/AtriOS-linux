OWNER(g:yandex_io)

UNITTEST_FOR(smart_devices/platforms/yandexstation_2/platformd/leds)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/tests/testlib
    yandex_io/tests/testlib/null_sdk
)

SRCS(
    test_color_config.cpp
    test_draw_led_screen_directive_observer.cpp
    test_gif_cache.cpp
    test_progress_update_gif.cpp
    test_read_gif.cpp
    test_timer_frontal_animation.cpp
    test_volume_frontal_animation.cpp
    test_rcu_volume_frontal_animation.cpp
)

END()
