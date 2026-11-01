OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
)

IF (NOT OS_NONE)
    PEERDIR(
        smart_devices/third_party/gecko_sdk/protocol/zigbee/app/ezsp-host
    )
ENDIF()

NO_COMPILER_WARNINGS()
NO_CLANG_TIDY()

SRCS(
    secure-ezsp-stub.c
)

IF (NOT OS_NONE)
    SRCS(
        ezsp-callbacks.c
        ezsp-enum-decode.c
        ezsp-frame-utilities.c
        ezsp.c
        serial-interface-uart.c
    )
ENDIF()

END()
