LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/radio/mac
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/common
    smart_devices/third_party/gecko_sdk/util/silicon_labs/silabs_core/buffer_manager
)

END()
