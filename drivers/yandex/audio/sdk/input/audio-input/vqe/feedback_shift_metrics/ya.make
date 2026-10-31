OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/telemetry
    yandex_io/libs/json_utils
    yandex_io/libs/logging
    yandex_io/modules/audio_input/vqe/engine
)

SRCS(
    feedback_shift_logger.cpp
)

END()

RECURSE_FOR_TESTS(tests)
