OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/audio_dumper
    yandex_io/modules/audio_input/broken_mic
    yandex_io/modules/audio_input/controller
    yandex_io/modules/audio_input/controller/audio_device/simple_controller
    yandex_io/modules/audio_input/controller/mics_state_proxy
    yandex_io/modules/audio_input/mics_state
    yandex_io/modules/audio_input/mics_state/hw_provider
    yandex_io/modules/audio_input/mics_state/metrica_listener

    yandex_io/sdk

    yandex_io/modules/audio_input/audio_device
    yandex_io/libs/base
    yandex_io/libs/device
    yandex_io/libs/json_utils
)

SRCS(
    audio_source_service.cpp
)

END()
