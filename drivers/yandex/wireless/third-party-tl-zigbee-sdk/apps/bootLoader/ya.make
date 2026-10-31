OWNER(g:yandex_io)

MCU_PROGRAM()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ALLOCATOR(FAKE)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/proj
)

SRCS(
    bootloader.c
    main.c
)

LDFLAGS(
    -fuse-ld=bfd

    -nostartfiles
)

END()
