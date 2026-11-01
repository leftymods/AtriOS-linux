LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp
)

NO_COMPILER_WARNINGS()

SRCS(
    command-handlers-secure-ezsp-stub.c
    #command-handlers-secure-ezsp.c
    em260-common.c
    serial-interface-uart.c
)

END()
