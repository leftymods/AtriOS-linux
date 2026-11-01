IF (TL_CONFIG)
    INCLUDE(${ARCADIA_ROOT}/${TL_CONFIG})
ELSE()
    MESSAGE(FATAL_ERROR "TL_CONFIG is required")
ENDIF()

NO_PLATFORM()

CFLAGS(
    -DMCU_CORE_B91=1

    -mcpu=d25f
    -mext-dsp
    -mabi=ilp32f
    -mcmodel=small

    -fpack-struct
    -fshort-enums
    -flto
    -ffunction-sections
    -fdata-sections

    -fomit-frame-pointer
    -fno-strict-aliasing
    -fshort-wchar
)

IF (TL_ZB_ROLE == "coordinator")
    CFLAGS(
        -DCOORDINATOR=1
    )
ELSEIF (TL_ZB_ROLE == "router")
    CFLAGS(
        -DROUTER=1
    )
ELSEIF (TL_ZB_ROLE == "end_device")
    CFLAGS(
        -DEND_DEVICE=1
    )
ENDIF()
