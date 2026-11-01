OWNER(g:yandex_io)

LIBRARY()

NO_CLANG_TIDY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp
)

SRCS(
    zigbee-device-common.c
    zigbee-device-host.c
)

END()
