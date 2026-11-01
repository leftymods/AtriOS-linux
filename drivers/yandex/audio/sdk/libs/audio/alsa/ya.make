LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/restricted/alsa-lib
    contrib/restricted/spdlog
    yandex_io/libs/audio/common
    yandex_io/libs/audio/reader
    yandex_io/libs/audio/writer
    yandex_io/libs/logging
)

SRCS(
    alsa_error.cpp
    alsa_logger.cpp
    alsa_audio_reader.cpp
    alsa_audio_writer.cpp
)


END()

RECURSE_FOR_TESTS(tests)
