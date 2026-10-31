OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/libs/audio_player/gstreamer)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/services/mediad/audioclient # steal stream_app_src
)

SRCS(
    test_gio_logging_resolver.cpp
    test_gstreamer_audio_player.cpp
)

END()
