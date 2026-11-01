OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/iot/tuya_smart_link
    smart_devices/libs/device_id/android_property

    yandex_io/libs/android_support/system_property
    yandex_io/libs/configuration
    yandex_io/libs/device
    yandex_io/libs/device_cryptography/keymaster_proxy
    yandex_io/libs/device_cryptography/plain_file
    yandex_io/libs/json_utils

    yandex_io/scaffolding/base
    yandex_io/scaffolding/telemetry/client
)

SRCS(
    device.cpp
    yandex_station2_hal.cpp
)

END()
