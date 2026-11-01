OWNER(g:yandex_io)

LIBRARY()

SET(AUDIOSENDER_VERSION_MAJOR 0)
SET(AUDIOSENDER_VERSION_MINOR 0)
SET(AUDIOSENDER_VERSION_PATCH 3)
CFLAGS(-DAUDIOSENDER_VERSION=\\"${AUDIOSENDER_VERSION_MAJOR}.${AUDIOSENDER_VERSION_MINOR}.${AUDIOSENDER_VERSION_PATCH}\\")

PEERDIR(
    contrib/deprecated/jsoncpp

    speechkit/core
    speechkit/linux/src/linux/platform_info

    yandex_io/capabilities/device_state/host
    yandex_io/libs/audio/common
    yandex_io/libs/base
    yandex_io/libs/configuration
    yandex_io/libs/device
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/libs/ring_buffer
    yandex_io/libs/speechkit_logger
    yandex_io/protos
    yandex_io/sdk
    yandex_io/sdk/private/remoting
    yandex_io/services/audiosender/info
)

SRCS(
    audio_config.cpp
    audio_sender_endpoint.cpp
    audio_sender_service.cpp
    audio_source_adapter.cpp
    command_server.cpp
    common.cpp
    discovery_agent.cpp
    log_sending_resul_sink.cpp
    log_streamer_state.cpp
    log_streamer.cpp
    phrase_spotter_wrapper.cpp
    platform_info.cpp
    sound_collector.cpp
    sound_player.cpp
    stream_sound_collector.cpp
    yandex_io_audio_source_adapter.cpp
    yandex_music_player.cpp
)

END()

RECURSE(
    info
)
