LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/protocol/zigbee/app/framework/generated
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/framework/include
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/framework/util
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/base/hal
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/common
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/serial
    smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/zigbee-framework
    smart_devices/third_party/gecko_sdk/util/silicon_labs/silabs_core/event_control
)

NO_COMPILER_WARNINGS()
NO_CLANG_TIDY()

CFLAGS(
    GLOBAL -DCONFIGURATION_HEADER=\"app/framework/util/config.h\"
    GLOBAL -DZA_GENERATED_HEADER=\"znet_config.h\"
    GLOBAL -DATTRIBUTE_STORAGE_CONFIGURATION=\"af_endpoint_config.h\"
)

SRCS(
    cli/core-cli.c
    cli/network-cli.c
    cli/option-cli.c
    cli/plugin-cli.c
    cli/security-cli.c
    cli/zcl-cli.c
    cli/zdo-cli.c
    security/af-node.c
    security/af-security-common.c
    security/af-trust-center.c
    security/crypto-state.c
    util/af-event.c
    util/af-main-common.c
    util/attribute-size.c
    util/attribute-storage.c
    util/attribute-table.c
    util/client-api.c
    util/message.c
    util/multi-network.c
    util/print-formatter.c
    util/print.c
    util/process-cluster-message.c
    util/process-global-message.c
    util/service-discovery-common.c
    util/time-util.c
    util/util.c

    plugin/concentrator/concentrator-support-cli.c
    plugin/concentrator/concentrator-support.c

    plugin/device-table/device-table-cli.c
    plugin/device-table/device-table-discovery.c
    plugin/device-table/device-table-tracking.c
    plugin/device-table/device-table.c

    plugin/network-creator/network-creator-cli.c
    plugin/network-creator/network-creator.c

    plugin/network-creator-security/network-creator-security-cli.c
    plugin/network-creator-security/network-creator-security.c

    plugin/scan-dispatch/scan-dispatch.c

    generated/callback-stub.c
    generated/call-command-handler.c
    generated/znet-bookkeeping.c
    generated/znet-cli.c
)

IF (OS_NONE)
    SRCS(
        util/af-main-soc.c
        util/service-discovery-soc.c
    )
ELSE()
    SRCS(
        util/af-main-host.c
        util/service-discovery-host.c

        plugin-host/ncp-configuration/ncp-configuration.c
    )
ENDIF()

END()
