SET(TL_ZB_ROLE end_device)

ADDINCL(
    smart_devices/third_party/tl_zigbee_sdk/apps/sampleSwitch
)

CFLAGS(
    -D__PROJECT_TL_SWITCH__=1
)
