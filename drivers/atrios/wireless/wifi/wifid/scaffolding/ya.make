OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/interfaces/device_state/connector
    yandex_io/interfaces/user_config/connector
    yandex_io/libs/base
    yandex_io/libs/device
    yandex_io/libs/ipc/mixed
    yandex_io/libs/logging
    yandex_io/libs/logging_handler
    yandex_io/libs/minidump
    yandex_io/libs/telemetry
    yandex_io/libs/telemetry_handler
    yandex_io/libs/terminate_waiter
    yandex_io/services/wifid
    yandex_io/scaffolding/base
)

SRCS(
    wifid.cpp
)

END()
