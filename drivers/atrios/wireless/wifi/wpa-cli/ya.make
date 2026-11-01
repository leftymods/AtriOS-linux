OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/restricted/patched/hostap_client
    yandex_io/libs/logging
    yandex_io/protos
)

SRCS(
    wpa_client_supplicant.cpp
)

END()
