OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/gecko_sdk/gecko.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/gecko_sdk/protocol/zigbee
    GLOBAL smart_devices/third_party/gecko_sdk/protocol/zigbee/stack
)

PEERDIR(
    smart_devices/third_party/gecko_sdk/platform/radio/mac
)

IF (SL_PLUGIN_BINDING_TABLE_LIBRARY == "yes")
    SRCS(../build/binding-table-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/binding-table-library.a)
ELSEIF (SL_PLUGIN_BINDING_TABLE_LIBRARY == "stub")
    SRCS(../build/binding-table-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/binding-table-stub-library.a)
ENDIF()

IF (SL_PLUGIN_CBKE_163K1_LIBRARY == "yes")
    SRCS(../build/cbke-163k1-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-163k1-library.a)
ELSEIF (SL_PLUGIN_CBKE_163K1_LIBRARY == "stub")
    SRCS(../build/cbke-163k1-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-163k1-stub-library.a)
ENDIF()

IF (SL_PLUGIN_CBKE_283K1_LIBRARY == "yes")
    SRCS(../build/cbke-283k1-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-283k1-library.a)
ELSEIF (SL_PLUGIN_CBKE_283K1_LIBRARY == "stub")
    SRCS(../build/cbke-283k1-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-283k1-stub-library.a)
ENDIF()

IF (SL_PLUGIN_CBKE_LIBRARY_CORE == "yes")
    SRCS(../build/cbke-library-core-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-library-core.a)
ELSEIF (SL_PLUGIN_CBKE_LIBRARY_CORE == "stub")
    SRCS(../build/cbke-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-stub-library.a)
ENDIF()

IF (SL_PLUGIN_CBKE_LIBRARY_DSA_SIGN == "yes")
    SRCS(../build/cbke-library-dsa-sign-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-library-dsa-sign.a)
ELSEIF (SL_PLUGIN_CBKE_LIBRARY_DSA_SIGN == "stub")
    SRCS(../build/cbke-library-dsa-sign-stub-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-library-dsa-sign-stub.a)
ENDIF()

IF (SL_PLUGIN_CBKE_LIBRARY_DSA_VERIFY == "yes")
    SRCS(../build/cbke-library-dsa-verify-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-library-dsa-verify.a)
ELSEIF (SL_PLUGIN_CBKE_LIBRARY_DSA_VERIFY == "stub")
    SRCS(../build/cbke-library-dsa-verify-stub-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-library-dsa-verify-stub.a)
ENDIF()

IF (SL_PLUGIN_CBKE_LIBRARY_DSA_VERIFY_283K1 == "yes")
    SRCS(../build/cbke-library-dsa-verify-283k1-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-library-dsa-verify-283k1.a)
ELSEIF (SL_PLUGIN_CBKE_LIBRARY_DSA_VERIFY_283K1 == "stub")
    SRCS(../build/cbke-library-dsa-verify-283k1-stub-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/cbke-library-dsa-verify-283k1-stub.a)
ENDIF()

IF (SL_PLUGIN_DEBUG_BASIC_LIBRARY == "yes")
    SRCS(../build/debug-basic-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/debug-basic-library.a)
ELSEIF (SL_PLUGIN_DEBUG_BASIC_LIBRARY == "stub")
    SRCS(../build/debug-basic-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/debug-basic-stub-library.a)
ENDIF()

IF (SL_PLUGIN_DEBUG_EXTENDED_LIBRARY == "yes")
    SRCS(../build/debug-extended-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/debug-extended-library.a)
ELSEIF (SL_PLUGIN_DEBUG_EXTENDED_LIBRARY == "stub")
    SRCS(../build/debug-extended-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/debug-extended-stub-library.a)
ENDIF()

IF (SL_PLUGIN_GP_LIBRARY == "yes")
    SRCS(../build/gp-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/gp-library.a)
ELSEIF (SL_PLUGIN_GP_LIBRARY == "stub")
    SRCS(../build/gp-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/gp-stub-library.a)
ENDIF()

IF (SL_PLUGIN_SOURCE_ROUTE_LIBRARY == "yes")
    SRCS(../build/source-route-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/source-route-library.a)
ELSEIF (SL_PLUGIN_SOURCE_ROUTE_LIBRARY == "stub")
    SRCS(../build/source-route-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/source-route-stub-library.a)
ENDIF()

IF (SL_PLUGIN_END_DEVICE_BIND_LIBRARY == "yes")
    SRCS(../build/end-device-bind-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/end-device-bind-library.a)
ELSEIF (SL_PLUGIN_END_DEVICE_BIND_LIBRARY == "stub")
    SRCS(../build/end-device-bind-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/end-device-bind-stub-library.a)
ENDIF()

IF (SL_PLUGIN_ZIGBEE_R22_SUPPORT_LIBRARY == "yes")
    SRCS(../build/zigbee-r22-support-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/zigbee-r22-support-library.a)
ELSEIF (SL_PLUGIN_ZIGBEE_R22_SUPPORT_LIBRARY == "stub")
    SRCS(../build/zigbee-r22-support-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/zigbee-r22-support-stub-library.a)
ENDIF()

IF (SL_PLUGIN_INSTALL_CODE_LIBRARY == "yes")
    SRCS(../build/install-code-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/install-code-library.a)
ELSEIF (SL_PLUGIN_INSTALL_CODE_LIBRARY == "stub")
    SRCS(../build/install-code-library-stub-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/install-code-library-stub.a)
ENDIF()

IF (SL_PLUGIN_MFGLIB_LIBRARY == "yes")
    SRCS(../build/mfglib-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/mfglib-library.a)
ELSEIF (SL_PLUGIN_MFGLIB_LIBRARY == "stub")
    SRCS(../build/mfglib-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/mfglib-stub-library.a)
ENDIF()

IF (SL_PLUGIN_MULTI_NETWORK_LIBRARY == "yes")
    SRCS(../build/multi-network-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/multi-network-library.a)
ELSEIF (SL_PLUGIN_MULTI_NETWORK_LIBRARY == "stub")
    SRCS(../build/multi-network-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/multi-network-stub-library.a)
ENDIF()

IF (SL_PLUGIN_MULTI_PAN_LIBRARY == "yes")
    SRCS(../build/multi-pan-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/multi-pan-library.a)
ELSEIF (SL_PLUGIN_MULTI_PAN_LIBRARY == "stub")
    SRCS(../build/multi-pan-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/multi-pan-stub-library.a)
ENDIF()

IF (SL_PLUGIN_PACKET_VALIDATE_LIBRARY == "yes")
    SRCS(../build/packet-validate-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/packet-validate-library.a)
ELSEIF (SL_PLUGIN_PACKET_VALIDATE_LIBRARY == "stub")
    SRCS(../build/packet-validate-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/packet-validate-stub-library.a)
ENDIF()

IF (SL_PLUGIN_SECURITY_LIBRARY_LINK_KEYS == "yes")
    SRCS(../build/security-library-link-keys-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/security-library-link-keys.a)
ELSEIF (SL_PLUGIN_SECURITY_LIBRARY_LINK_KEYS == "stub")
    SRCS(../build/security-library-link-keys-stub-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/security-library-link-keys-stub.a)
ENDIF()

IF (SL_PLUGIN_ZIGBEE_PRO_STACK == "yes")
    SRCS(../build/zigbee-pro-stack-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/zigbee-pro-stack.a)
ENDIF()

IF (SL_PLUGIN_ZIGBEE_PRO_STACK_ALT_MAC == "yes")
    SRCS(../build/zigbee-pro-stack-alt-mac-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/zigbee-pro-stack-alt-mac.a)
ENDIF()

IF (SL_PLUGIN_ZIGBEE_PRO_LEAF_LIBRARY == "yes")
    SRCS(../build/zigbee-pro-leaf-stack-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/zigbee-pro-leaf-stack.a)
ENDIF()

IF (SL_PLUGIN_ZLL_LIBRARY == "yes")
    SRCS(../build/zll-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/zll-library.a)
ELSEIF (SL_PLUGIN_ZLL_LIBRARY == "stub")
    SRCS(../build/zll-stub-library-cortexm3-gcc-${SL_DEVICE_FAMILY}-rail/zll-stub-library.a)
ENDIF()

END()
