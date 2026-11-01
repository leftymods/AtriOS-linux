OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/telemetry
    yandex_io/modules/audio_input/vqe/controller
    yandex_io/modules/audio_input/vqe/engine/yandex_c
)

SRCS(
    vqe_controller_with_yandex_vqe.cpp
)

END()
