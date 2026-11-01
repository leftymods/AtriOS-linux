OWNER(g:yandex_io)

MCU_PROGRAM()

NO_CLANG_TIDY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ALLOCATOR(FAKE)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/platform/base/hal/plugin/antenna-stub
    smart_devices/third_party/gecko_sdk/platform/base/hal/plugin/debug-jtag-stub
    smart_devices/third_party/gecko_sdk/platform/base/hal/plugin/serial
    smart_devices/third_party/gecko_sdk/platform/base/hal/plugin/sim-eeprom2-to-nvm3-upgrade
    smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/plugin/coexistence
)

LDFLAGS(
    --specs=nosys.specs --specs=nano.specs
    -lnosys
)

SRCS(
    main.c
)

END()
