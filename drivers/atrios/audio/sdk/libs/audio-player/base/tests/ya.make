OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/libs/audio_player/base)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

SRCS(
    test_audio_player_params.cpp
    test_utils.cpp
)

END()
