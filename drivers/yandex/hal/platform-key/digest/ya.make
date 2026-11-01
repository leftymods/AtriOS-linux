OWNER(g:yandex_io)

UNION()

PEERDIR(
    smart_devices/libs/platform_key
)

COPY_FILE(
    ${ARCADIA_BUILD_ROOT}/smart_devices/libs/platform_key/platform_key.sha256
    platform_key.sha256
)

END()
