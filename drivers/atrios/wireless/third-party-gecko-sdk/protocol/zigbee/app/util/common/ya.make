OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

NO_COMPILER_WARNINGS()

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
)

SRCS(
    library.c
)

END()
