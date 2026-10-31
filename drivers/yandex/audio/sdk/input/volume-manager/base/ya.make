OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/bluetooth/bluetooth_state_listener
    yandex_io/modules/bluetooth/bluetooth_observer
    yandex_io/libs/configuration
    yandex_io/libs/logging
    yandex_io/libs/threading
    yandex_io/sdk
)

SRCS(
    volume_manager.cpp
    volume_metrics.cpp
    i_volume_manager_listener.cpp
)

END()

RECURSE_FOR_TESTS(tests)
