OWNER(g:yandex_io)

LIBRARY()

SRCS(
    i_audio_input_controller.cpp
)

END()

RECURSE(
    audio_device
    dialog_mode
    mics_state_proxy
    multiple_vqe_controller
)
