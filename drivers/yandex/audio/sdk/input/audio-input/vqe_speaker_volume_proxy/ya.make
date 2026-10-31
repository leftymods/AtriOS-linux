OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/controller
    yandex_io/modules/audio_input/vqe/controller
    yandex_io/modules/volume_manager/base
)

SRCS(
    vqe_controller_speaker_volume_proxy.cpp
)

END()
