OWNER(g:yandex_io)

RECURSE(
    bootloader
    hci
    swire
)

IF (OS_NONE)
    RECURSE(
        mfg_test
    )
ENDIF()
