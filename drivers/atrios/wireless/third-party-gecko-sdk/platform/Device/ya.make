LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/platform/Device/SiliconLabs/${SL_DEVICE_FAMILY_U}/Include
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/CMSIS
)

SRCDIR(smart_devices/third_party/gecko_sdk/platform/Device/SiliconLabs/${SL_DEVICE_FAMILY_U})

SRCS(
    Source/system_${SL_DEVICE_FAMILY}.c
)

IF (SL_DEVICE_STARTUP)
    SRCS(
        Source/GCC/startup_${SL_DEVICE_FAMILY}.c
    )
ENDIF()

END()
