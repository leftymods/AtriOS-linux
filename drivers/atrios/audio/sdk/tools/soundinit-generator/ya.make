OWNER(g:yandex_io)

PROGRAM(soundinit_generator)

PEERDIR(
    contrib/restricted/boost/libs/program_options

    yandex_io/libs/logging
    yandex_io/libs/setup_parser
)

SRCS(
    soundinit_generator.cpp
)

END()
