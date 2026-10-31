OWNER(g:yandex_io)

LIBRARY()

SRCS(
    audio_device.cpp
)

END()

RECURSE(
    alsa
    base
    null
    stats
    tcp
    websocket
    yandex_vqe
)
