OWNER(g:yandex_io)

MCU_PROGRAM()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ALLOCATOR(FAKE)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/proj
    smart_devices/third_party/tl_zigbee_sdk/zbhci
)

SRCS(
    hci.c
    main.c
    mfg.c
)

LDFLAGS(
    -fuse-ld=bfd

    -nostartfiles
)

END()
