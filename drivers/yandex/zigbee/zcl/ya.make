OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/zigbee/proto

    yandex_io/libs/logging
)

SRCS(
    i_cluster.cpp
    types.cpp
    zcl_stream.cpp

    generated/clusters.cpp
    generated/generic.cpp
    generated/types.cpp
)

END()

IF (OS_LINUX AND ARCH_X86_64)
    RECURSE_FOR_TESTS(
        generated
    )
ENDIF()
