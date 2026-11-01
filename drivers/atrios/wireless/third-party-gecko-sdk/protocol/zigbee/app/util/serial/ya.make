OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

NO_CLANG_TIDY()
NO_COMPILER_WARNINGS()

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/protocol/zigbee/app/framework/generated
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
)

CFLAGS(
    GLOBAL -DCONFIGURATION_HEADER=\"app/framework/util/config.h\"
    GLOBAL -DZA_GENERATED_HEADER=\"znet_config.h\"
    GLOBAL -DATTRIBUTE_STORAGE_CONFIGURATION=\"af_endpoint_config.h\"

    -DNO_READLINE
)

SRCS(
    command-interpreter2.c
    linux-serial.c

    ../../../../../platform/base/hal/plugin/serial/ember-printf-convert.c
)

END()
