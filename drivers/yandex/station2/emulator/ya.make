OWNER(g:yandex_io)

PROGRAM(maind)

ALLOCATOR(SYSTEM)

EXPORTS_SCRIPT(${ARCADIA_ROOT}/yandex_io/misc/maind.exports)

PEERDIR(
    smart_devices/platforms/yandexstation_2/device
    smart_devices/platforms/yandexstation_2/platformd
    
    yandex_io/libs/device
    yandex_io/libs/dispatch_main
    yandex_io/libs/keymaster_proxy_app
    yandex_io/libs/quasar_client

    yandex_io/scaffolding/base
    yandex_io/scaffolding/telemetry
    yandex_io/scaffolding/yiod
    yandex_io/scaffolding/mediad
    yandex_io/scaffolding/updatesd
    yandex_io/scaffolding/glagold
)

SRCS(
    maind.cpp
)

END()

RECURSE(
    packages
)
