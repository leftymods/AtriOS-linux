LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

SRCS(
    ../../../../../protocol/zigbee/build/sim-eeprom2-to-nvm3-upgrade-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/sim-eeprom2-to-nvm3-upgrade-stub-library.a
)

END()
