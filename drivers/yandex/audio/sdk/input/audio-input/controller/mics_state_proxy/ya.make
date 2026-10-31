OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/json_utils
    yandex_io/libs/logging

    yandex_io/modules/audio_input/controller
    yandex_io/modules/audio_input/mics_state

    yandex_io/sdk
)

SRCS(
    audio_input_controller_mics_state_proxy.cpp
    mute_on_update_mics_state_proxy.cpp
)

END()
