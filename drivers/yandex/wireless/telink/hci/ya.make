OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    smart_devices/libs/tty

    yandex_io/libs/base
    yandex_io/libs/logging
)

SRCS(
    hci_command.cpp
    hci_command_handler.cpp
    hci.cpp
    mfg_commands.cpp
    zigbee_commands.cpp
)

END()
