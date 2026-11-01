LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/mbedtls/include
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_alt/include
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_mbedtls_support/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emdrv/nvm3
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/platform/radio/rail_lib
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/config
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/se_manager
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_protocol_crypto
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_psa_driver
)

SRCS(
    source/sl_entropy.c
    source/sl_mbedtls.c
    source/sl_psa_its_nvm3.c
    source/sl_se_management.c
)

END()
