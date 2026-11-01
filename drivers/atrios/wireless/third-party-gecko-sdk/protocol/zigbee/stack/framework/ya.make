OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/util/third_party/crypto/mbedtls
)

SRCS(
    ccm-star.c
    strong-random-api.c
)

END()
