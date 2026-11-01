PROGRAM()

OWNER(akastornov)

ALLOCATOR(SYSTEM)

PEERDIR(
    smart_devices/platforms/yandexstation_2/platformd/leds/utils
    library/cpp/getopt
    library/cpp/resource
)

RESOURCE(
    smart_devices/platforms/yandexstation_2/data/frontal_animation/fonts/clock_small.gif clock_small
    smart_devices/platforms/yandexstation_2/data/frontal_animation/fonts/clock_middle.gif clock_middle
    smart_devices/platforms/yandexstation_2/data/frontal_animation/fonts/clock_large.gif clock_large
)

SRCS(main.cpp)

END()
