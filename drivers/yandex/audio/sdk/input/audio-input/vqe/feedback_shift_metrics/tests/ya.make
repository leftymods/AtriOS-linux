OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/modules/audio_input/vqe/feedback_shift_metrics)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/libs/telemetry/null
    yandex_io/libs/json_utils
    library/cpp/testing/gmock_in_unittest
)

SRCS(
    test_feedback_shift_logger.cpp
)

END()
