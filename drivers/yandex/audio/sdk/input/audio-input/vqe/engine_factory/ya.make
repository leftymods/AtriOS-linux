OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/audio/reader
    yandex_io/modules/audio_input/vqe/engine
)

SRCS(
    vqe_engine_factory.cpp
)

END()

RECURSE_FOR_TESTS(
    tests
)
