LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/json_utils
    smart_devices/third_party/mediatek/mt8516
)

SRCS(
    mt8516_bluetooth_impl.cpp
)

END()
