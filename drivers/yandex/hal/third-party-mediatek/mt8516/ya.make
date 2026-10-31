OWNER(g:yandex_io)

LIBRARY()

NO_UTIL()
NO_CLANG_TIDY()

IF (ARCH_ARM7)
    PEERDIR(smart_devices/third_party/mediatek/mt8516/dynamic_lib)
ELSE()
    PEERDIR(smart_devices/third_party/mediatek/mt8516/stubs)
ENDIF()

END()

IF (ARCH_ARM7)
    RECURSE(dynamic_lib)
ENDIF()

RECURSE(stubs)
