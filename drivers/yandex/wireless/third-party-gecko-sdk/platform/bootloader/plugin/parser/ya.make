OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/bootloader/core
)

SRCS(
    btl_image_parser.c

    ebl/btl_gbl_custom_tags.c

    ../../build/lib/libparser_efr32xg21_gcc.a
)

END()

RECURSE(
    app_version
)
