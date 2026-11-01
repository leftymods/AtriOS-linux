LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/libs/tinyalsa
    yandex_io/libs/audio/reader
    yandex_io/libs/audio/writer
    yandex_io/libs/logging
)

# pcm_read(), pcm_write() are marked as deprecated in tinyalsa
CFLAGS(-Wno-deprecated-declarations)

SRCS(
    tiny_alsa_audio_reader.cpp
    tiny_alsa_audio_writer.cpp
)

END()
