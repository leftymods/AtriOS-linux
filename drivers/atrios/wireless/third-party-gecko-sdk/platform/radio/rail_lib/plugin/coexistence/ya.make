LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/plugin/coexistence/common
    smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/plugin/coexistence/config
    smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/plugin/coexistence/config/efr32xg2x
    smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/plugin/coexistence/hal/efr32
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/platform/emdrv/gpiointerrupt
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/platform/radio/rail_lib
)

SRCS(
    protocol/ieee802154/coexistence-802154.c
    protocol/ieee802154/coulomb-counter-802154.c
)

END()
