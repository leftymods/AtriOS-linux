OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    contrib/libs/fmt
    contrib/restricted/boost
    yandex_io/libs/base
    yandex_io/libs/equalizer_config
    yandex_io/libs/threading
)

SRCS(
    audio_clock.cpp
    audio_player.cpp
    stream_src.cpp
    utils.cpp
)

END()

RECURSE_FOR_TESTS(tests)
