OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/third_party/tuya_sdk

    yandex_io/libs/iot
    yandex_io/libs/logging
)

SRCS(
    iot_discovery_impl.cpp
    iot_discovery_provider_impl.cpp
)

END()
