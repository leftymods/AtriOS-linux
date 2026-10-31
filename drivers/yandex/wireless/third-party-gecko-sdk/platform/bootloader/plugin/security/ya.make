OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/bootloader/core
)

SRCS(
    btl_crc16.c
    btl_crc32.c
    btl_security_aes.c
    btl_security_ecdsa.c
    btl_security_sha256.c
    btl_security_tokens.c

    ecc/ecc.c

    sha/btl_sha256.c
    sha/cryptoacc_sha.c
    sha/crypto_sha.c
    sha/se_sha.c
)

END()
