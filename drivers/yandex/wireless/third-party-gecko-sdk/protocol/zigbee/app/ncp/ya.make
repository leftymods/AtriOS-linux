OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

SRCS(
    ../../build/em260-uart-util-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/em260-uart-util-library.a
    ../../build/ncp-ash-fifo-xon-xoff-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-ash-fifo-xon-xoff-library.a
    ../../build/ncp-binding-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-binding-library.a
    ../../build/ncp-cbke-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-cbke-library.a
    ../../build/ncp-gp-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-gp-library.a
    ../../build/ncp-mfglib-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-mfglib-library.a
    ../../build/ncp-pro-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-pro-library.a
    ../../build/ncp-security-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-security-library.a
    ../../build/ncp-source-route-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-source-route-library.a
    ../../build/ncp-zll-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/ncp-zll-library.a
)

END()
