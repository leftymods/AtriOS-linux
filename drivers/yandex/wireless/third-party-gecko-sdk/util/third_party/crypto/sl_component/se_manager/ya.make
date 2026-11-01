LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/se_manager/inc
    GLOBAL smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/se_manager/src
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/common
    smart_devices/third_party/gecko_sdk/platform/emlib
)

SRCS(
    src/sl_se_manager.c
    src/sl_se_manager_attestation.c
    src/sl_se_manager_cipher.c
    src/sl_se_manager_entropy.c
    src/sl_se_manager_hash.c
    src/sl_se_manager_key_derivation.c
    src/sl_se_manager_key_handling.c
    src/sl_se_manager_signature.c
    src/sl_se_manager_util.c
)

END()
