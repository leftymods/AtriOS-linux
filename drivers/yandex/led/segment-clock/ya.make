OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/logging

    yandex_io/sdk
    yandex_io/modules/clock_display_state_holder
    
    smart_devices/libs/brightness_control
)

SRCS(
    clock_state_handler.cpp
)

END()
