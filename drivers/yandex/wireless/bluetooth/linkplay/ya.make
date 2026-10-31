LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/restricted/alsa-lib
    smart_devices/third_party/linkplay_bluetooth
    yandex_io/libs/base
    yandex_io/libs/audio/alsa
    yandex_io/libs/threading
)

SRCS(
    bluetooth_impl_constants.cpp
    linkplay_a98_bluetooth_impl.cpp
    private/source_stream_sender.cpp
    private/stream_alsa_writer.cpp
)

END()
