LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/config
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_mbedtls_support/config
    GLOBAL smart_devices/third_party/gecko_sdk/util/plugin/plugin-common/mbedtls
)

IF (SL_MBEDTLS_CONFIG_FILE)
    CFLAGS(GLOBAL -DMBEDTLS_CONFIG_FILE=\"\\\"${SL_MBEDTLS_CONFIG_FILE}\\\"\")
ENDIF()

CFLAGS(
    GLOBAL -DMBEDTLS_DEVICE_ACCELERATION_CONFIG_FILE=\"config-device-acceleration.h\"
    GLOBAL -DMBEDTLS_DEVICE_ACCELERATION_CONFIG_APP_FILE=\"config-device-acceleration-app.h\"
)

END()
