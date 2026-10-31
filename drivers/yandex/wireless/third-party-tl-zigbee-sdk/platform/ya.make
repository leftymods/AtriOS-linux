OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/platform
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/platform/riscv
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/proj
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/proj/common
)

LINKER_SCRIPT(
    boot/b91/boot_b91.ld
)

SRCS(
    GLOBAL boot/link_cfg.S

    boot/b91/cstartup_b91.S

    chip_b91/flash.c
    chip_b91/plic.c

    lib/libdrivers_b91.a

    riscv/libfirmware_encrypt.a

    GLOBAL services/b91m/irq_handler.c
)

END()
