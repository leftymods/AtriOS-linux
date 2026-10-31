ADDINCL(
    smart_devices/libs/telink/bootloader/bin
)

CFLAGS(
    -D__PROJECT_TL_BOOT_LOADER__=1
)

IF (TL_DEBUG == "yes")
    CFLAGS(
        -DUART_PRINTF_MODE=1
    )
ENDIF()
