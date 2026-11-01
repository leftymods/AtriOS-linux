SET(TL_ZB_ROLE router)

ADDINCL(
    smart_devices/third_party/tl_zigbee_sdk/apps/sampleLight
)

CFLAGS(
    -D__PROJECT_TL_DIMMABLE_LIGHT__=1
)
