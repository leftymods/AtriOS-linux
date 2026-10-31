LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/mbedtls/include
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/mbedtls/library
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_mbedtls_support/inc
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_psa_driver/inc
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/emlib
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/config
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/se_manager
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_cryptoacc_library
)

SRCS(
    src/crypto_management.c
    src/cryptoacc_management.c
    src/sli_crypto_transparent_driver_aead.c
    src/sli_crypto_transparent_driver_cipher.c
    src/sli_crypto_transparent_driver_hash.c
    src/sli_crypto_transparent_driver_mac.c
    src/sli_crypto_trng_driver.c
    src/sli_cryptoacc_transparent_driver_aead.c
    src/sli_cryptoacc_transparent_driver_cipher.c
    src/sli_cryptoacc_transparent_driver_hash.c
    src/sli_cryptoacc_transparent_driver_key_derivation.c
    src/sli_cryptoacc_transparent_driver_key_management.c
    src/sli_cryptoacc_transparent_driver_mac.c
    src/sli_cryptoacc_transparent_driver_signature.c
    src/sli_psa_driver_common.c
    src/sli_psa_driver_ghash.c
    src/sli_psa_driver_init.c
    src/sli_psa_trng.c
    src/sli_se_driver_aead.c
    src/sli_se_driver_builtin_keys.c
    src/sli_se_driver_cipher.c
    src/sli_se_driver_key_derivation.c
    src/sli_se_driver_key_management.c
    src/sli_se_driver_mac.c
    src/sli_se_driver_signature.c
    src/sli_se_opaque_driver_aead.c
    src/sli_se_opaque_driver_cipher.c
    src/sli_se_opaque_driver_mac.c
    src/sli_se_opaque_key_derivation.c
    src/sli_se_transparent_driver_aead.c
    src/sli_se_transparent_driver_cipher.c
    src/sli_se_transparent_driver_hash.c
    src/sli_se_transparent_driver_mac.c
    src/sli_se_transparent_key_derivation.c
)

END()
