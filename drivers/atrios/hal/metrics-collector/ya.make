OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp

    yandex_io/external_libs/datacratic
    yandex_io/libs/base
    yandex_io/libs/device
    yandex_io/libs/logging
    yandex_io/libs/metrica/ubi
    yandex_io/metrica/monitor/metrics_collector
    yandex_io/metrica/monitor/metrics_collector/ping_manager
)

SRCS(
    metrics_collector_impl.cpp
)

END()
