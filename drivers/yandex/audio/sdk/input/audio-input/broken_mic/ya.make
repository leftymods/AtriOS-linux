OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/modules/audio_input/audio_device
    yandex_io/libs/base
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/sdk
    contrib/deprecated/jsoncpp
)

SRCS(
    broken_mic_config_handler.cpp
    broken_mic_detector.cpp
    broken_mic_detector_system_holder.cpp
    broken_mic_first_configure_handler.cpp
    broken_mic_logger.cpp
)

END()
