OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/capabilities/device_state/interfaces
    yandex_io/modules/audio_input/mics_state
    yandex_io/sdk
)

SRCS(
    mics_state_sdk_notifier.cpp
)

END()
