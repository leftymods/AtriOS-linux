LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/chip/efr32/efr32xg2x
    GLOBAL smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/common
    GLOBAL smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/plugin
    GLOBAL smart_devices/third_party/gecko_sdk/platform/radio/rail_lib/protocol/ieee802154
)

IF (SL_DEVICE_IS_MODULE == "yes")
    SRCS(
        autogen/librail_release/librail_config_${SL_DEVICE}_gcc.a
        autogen/librail_release/librail_module_efr32xg21_gcc_release.a
    )
ELSE()
    SRCS(
        autogen/librail_release/librail_efr32xg21_gcc_release.a
    )
ENDIF()

END()
