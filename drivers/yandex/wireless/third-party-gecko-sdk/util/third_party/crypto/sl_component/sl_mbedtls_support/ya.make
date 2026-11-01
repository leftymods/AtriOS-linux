LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/mbedtls/include
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_mbedtls_support/config
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_mbedtls_support/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/config
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_alt
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_protocol_crypto
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_psa_driver
)

SRCS(
    src/aes_aes.c
    src/crypto_aes.c
    src/crypto_ecp.c
    src/crypto_gcm.c
    src/cryptoacc_aes.c
    src/cryptoacc_ccm.c
    src/cryptoacc_gcm.c
    src/mbedtls_cmac.c
    src/mbedtls_ecdsa_ecdh.c
    src/mbedtls_sha.c
    src/se_aes.c
    src/se_ccm.c
    src/se_gcm.c
    src/se_jpake.c
)

END()
