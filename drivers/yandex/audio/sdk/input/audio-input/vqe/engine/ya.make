OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/device
)

SRCS(
    vqe_engine.cpp
)

END()

RECURSE(
    yandex_c
)
