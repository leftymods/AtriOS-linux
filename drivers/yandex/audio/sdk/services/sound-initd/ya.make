OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/capabilities/file_player
    yandex_io/interfaces/device_state
    yandex_io/libs/device
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/setup_parser
    yandex_io/modules/audio_input/callback_listener
    yandex_io/protos/enum_names
    yandex_io/sdk
)

SRCS(
    sound_init_endpoint.cpp
    sound_init_service.cpp
)

END()

RECURSE_FOR_TESTS(tests)
