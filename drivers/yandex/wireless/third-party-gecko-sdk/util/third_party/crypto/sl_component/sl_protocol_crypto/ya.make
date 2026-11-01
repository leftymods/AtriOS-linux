LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/mbedtls/include
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_protocol_crypto/src
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/common
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/se_manager
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_psa_driver
)

SRCS(
    src/sli_protocol_crypto_crypto.c
    src/sli_protocol_crypto_radioaes.c
    src/sli_radioaes_management.c
)

END()
