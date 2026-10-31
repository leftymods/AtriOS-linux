OWNER(g:yandex_io)

MCU_PROGRAM(bootloader)

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ALLOCATOR(FAKE)

PEERDIR(
    smart_devices/libs/telink/bootloader
)

SRCS(
    irq.c
    main.c
)

LDFLAGS(
    -fuse-ld=bfd

    -nostartfiles
)

END()
