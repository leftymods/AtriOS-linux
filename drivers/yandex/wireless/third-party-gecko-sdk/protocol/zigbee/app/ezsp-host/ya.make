OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

NO_COMPILER_WARNINGS()
NO_CLANG_TIDY()

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/gateway
)

SRCS(
    ezsp-host-io.c
    ezsp-host-queues.c
    ezsp-host-ui.c
    ash/ash-host.c
    ash/ash-host-ui.c
)

END()
