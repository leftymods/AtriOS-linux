LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
)

NO_COMPILER_WARNINGS()

SRCS(
    ember-configuration.c
)

END()
