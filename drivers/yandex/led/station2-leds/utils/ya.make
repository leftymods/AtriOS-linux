LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    yandex_io/libs/errno
    yandex_io/libs/filesystem
    yandex_io/libs/logging
    yandex_io/libs/json_utils
    contrib/libs/giflib
)

SRCS(
    config_utils.cpp
    draw_utils.cpp
    gif_recorder.cpp
    gif_utils.cpp
)

END()
