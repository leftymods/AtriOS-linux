OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/backend_config_observer
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/libs/subprocess
    yandex_io/libs/telemetry
    yandex_io/sdk

    library/cpp/ipv6_address
)

SRCS(
    dhcpcd_dns_updater.cpp
    dns_health_checker.cpp
    installer.cpp
    runtime_dns_config.cpp
)

END()

RECURSE_FOR_TESTS(tests)
