OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/services/mediad/audioclient)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/libs/base
    yandex_io/libs/audio_player/gstreamer
    yandex_io/libs/protobuf_utils
    yandex_io/services/mediad/audioclient
    library/cpp/testing/gmock_in_unittest
)

SRCS(
    test_audio_client.cpp
    test_audio_client_controller.cpp
    test_audio_client_params.cpp
    test_audio_client_utils.cpp
)

REQUIREMENTS(ram:9)

END()
