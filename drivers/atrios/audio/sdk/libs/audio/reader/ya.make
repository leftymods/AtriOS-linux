OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    yandex_io/libs/device
    yandex_io/libs/json_utils
)

SRCS(
    audio_reader.cpp
    audio_reader_config.cpp
)

END()
