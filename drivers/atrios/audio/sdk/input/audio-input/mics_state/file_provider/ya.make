OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/mics_state
    yandex_io/libs/base
    yandex_io/libs/logging
)

SRCS(
    file_mics_state_provider.cpp
    i_toggle_mics_state_provider.cpp
    stub_toggle_mics_state_provider.cpp
)

END()
