SET(TL_ZB_ROLE coordinator)

ADDINCL(
    smart_devices/third_party/tl_zigbee_sdk/apps/sampleGw
)

CFLAGS(
    -D__PROJECT_TL_GW__=1
)
