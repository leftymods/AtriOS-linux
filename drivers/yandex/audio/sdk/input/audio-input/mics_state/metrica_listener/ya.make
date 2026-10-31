OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/mics_state
    yandex_io/libs/telemetry
)

SRCS(
    metrica_mics_state_listener.cpp
)

END()
