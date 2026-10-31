OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

NO_COMPILER_WARNINGS()
NO_CLANG_TIDY()

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/base
    GLOBAL smart_devices/third_party/gecko_sdk/platform/base/hal
    GLOBAL smart_devices/third_party/gecko_sdk/platform/base/hal/plugin
    GLOBAL smart_devices/third_party/gecko_sdk/platform/base/hal/micro/cortexm3/efm32
    GLOBAL smart_devices/third_party/gecko_sdk/platform/base/hal/micro/cortexm3/efm32/config
)

IF (OS_NONE)
    PEERDIR(
        smart_devices/third_party/gecko_sdk/hardware/module
        smart_devices/third_party/gecko_sdk/platform/bootloader/api
        smart_devices/third_party/gecko_sdk/platform/emdrv/common
        smart_devices/third_party/gecko_sdk/platform/emdrv/gpiointerrupt
        smart_devices/third_party/gecko_sdk/platform/emdrv/nvm3
        smart_devices/third_party/gecko_sdk/platform/emdrv/spidrv
        smart_devices/third_party/gecko_sdk/platform/emdrv/tempdrv
        smart_devices/third_party/gecko_sdk/platform/emdrv/uartdrv
        smart_devices/third_party/gecko_sdk/platform/emdrv/ustimer
        smart_devices/third_party/gecko_sdk/platform/emlib
        smart_devices/third_party/gecko_sdk/platform/halconfig
        smart_devices/third_party/gecko_sdk/platform/radio/rail_lib
        smart_devices/third_party/gecko_sdk/protocol/zigbee/stack
        smart_devices/third_party/gecko_sdk/util/third_party/crypto/sl_component/sl_alt
    ) 

    CFLAGS(
        GLOBAL -DPLATFORM_HEADER=\"\\\"${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/platform/base/hal/micro/cortexm3/compiler/gcc.h\\\"\"

        GLOBAL -DCORTEXM3
        GLOBAL -DCORTEXM3_EFR32
        GLOBAL -DCORTEXM3_EFR32_MICRO
        GLOBAL -DCORTEXM3_EFM32_MICRO
        GLOBAL -DEFR32_SERIES2_CONFIG1_MICRO

        GLOBAL -DPHY_RAIL
        GLOBAL -DHAL_CONFIG
        GLOBAL -DUSE_NVM3

        -DPSSTORE_SIZE=${SL_PSSTORE_SIZE}
        -DLONGTOKEN_SIZE=${SL_LONGTOKEN_SIZE}
        -DLOCKBITS_IN_MAINFLASH_SIZE=${SL_LOCKBITS_IN_MAINFLASH_SIZE}
    )

    LDFLAGS(
        -Xlinker --defsym="SIMEEPROM_SIZE=${SL_SIMEEPROM_SIZE}"
        -Xlinker --defsym="PSSTORE_SIZE=${SL_PSSTORE_SIZE}"
        -Xlinker --defsym="LONGTOKEN_SIZE=${SL_LONGTOKEN_SIZE}"
        -Xlinker --defsym="LOCKBITS_IN_MAINFLASH_SIZE=${SL_LOCKBITS_IN_MAINFLASH_SIZE}"
        -Xlinker --defsym="FLASH_SIZE=${SL_FLASH_SIZE}"
        -Xlinker --defsym="RAM_SIZE=${SL_RAM_SIZE}"
        -Xlinker --defsym="FLASH_PAGE_SIZE=${SL_FLASH_PAGE_SIZE}"
        -Xlinker --defsym="EMBER_MALLOC_HEAP_SIZE=${SL_EMBER_MALLOC_HEAP_SIZE}"
        -Xlinker --defsym="HEADER_SIZE=${SL_HEADER_SIZE}"
        -Xlinker --defsym="BTL_SIZE=${SL_BTL_SIZE}"
    )

    LINKER_SCRIPT(
        micro/cortexm3/efm32/gcc-cfg.ld
        micro/cortexm3/gcc.ld
    )

    NO_COMPILER_WARNINGS()

    SRCS(
        micro/cortexm3/efm32/assert-crash-handlers.c
        micro/cortexm3/efm32/bootloader-interface-standalone.c
        micro/cortexm3/efm32/bootloader-interface.c
        micro/cortexm3/efm32/cstartup-common.c
        micro/cortexm3/efm32/faults-v7m.s79
        micro/cortexm3/efm32/hal-config.c
        micro/cortexm3/efm32/hal-config-gpio.c
        micro/cortexm3/efm32/isr-stubs.s79
        micro/cortexm3/efm32/led.c
        micro/cortexm3/efm32/long-token.c
        micro/cortexm3/efm32/mfg-token.c
        micro/cortexm3/efm32/micro-common.c
        micro/cortexm3/efm32/micro.c
        micro/cortexm3/efm32/sleep-efm32.c
        micro/cortexm3/efm32/token.c
        micro/generic/crc.c
        micro/generic/endian.c
        micro/generic/mem-util.c
        micro/generic/random.c
        micro/generic/token-def.c

        ../../../protocol/zigbee/build/hal-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/hal-library.a
    )
ELSE()
    ADDINCL(
        GLOBAL smart_devices/third_party/gecko_sdk/platform/base/hal/stub
    )

    PEERDIR(
        smart_devices/third_party/gecko_sdk/platform/bootloader/api
        smart_devices/third_party/gecko_sdk/platform/emlib
        smart_devices/third_party/gecko_sdk/platform/emdrv/common
        smart_devices/third_party/gecko_sdk/platform/emdrv/uartdrv
        smart_devices/third_party/gecko_sdk/protocol/zigbee/stack
    )

    CFLAGS(
        GLOBAL -DPLATFORM_HEADER=\"\\\"${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/platform/base/hal/micro/unix/compiler/gcc.h\\\"\"

        GLOBAL -DUNIX
        GLOBAL -DUNIX_HOST
        GLOBAL -DPHY_NULL
        GLOBAL -DEMBER_STACK_ZIGBEE
        GLOBAL -DEZSP_HOST
        GLOBAL -DEZSP_ASH
    )

    SRCS(
        micro/generic/ash-common.c
        micro/generic/crc.c
        micro/generic/endian.c
        micro/generic/random.c
        micro/generic/system-timer.c
        micro/unix/host/micro.c
    )
ENDIF()

END()
