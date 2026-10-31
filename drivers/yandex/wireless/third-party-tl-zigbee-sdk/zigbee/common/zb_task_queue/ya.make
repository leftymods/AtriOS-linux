OWNER(g:yandex_io)

LIBRARY()

INCLUDE(${ARCADIA_ROOT}/smart_devices/third_party/tl_zigbee_sdk/tl.inc)

ADDINCL(
    GLOBAL smart_devices/third_party/tl_zigbee_sdk/zigbee/common/includes
)

PEERDIR(
    smart_devices/third_party/tl_zigbee_sdk/proj
)

SRCS(
    zb_task_queue.c
    zb_task_queue.o
)

END()
