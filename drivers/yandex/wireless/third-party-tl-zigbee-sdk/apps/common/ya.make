OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/apps/common
)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/proj
    smart_devices/third_party/tl_zigbee_sdk/zigbee
)

SRCS(
    factory_reset.c
    firmwareEncryptChk.c
    main.c
    module_test.c
)

END()
