OWNER(g:yandex_io)

MCU_PROGRAM()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ALLOCATOR(FAKE)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/apps/common
    smart_devices/third_party/tl_zigbee_sdk/proj
    smart_devices/third_party/tl_zigbee_sdk/zbhci
    smart_devices/third_party/tl_zigbee_sdk/zigbee
)

SRCS(
    app_ui.c
    sampleSensor.c
    sampleSensorEpCfg.c
    zb_appCb.c
    zcl_sampleSensorCb.c
)

LDFLAGS(
    -fuse-ld=bfd

    -nostartfiles
)

END()
