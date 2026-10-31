OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/services/mediad/media/players/yandexmusic)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

SIZE(MEDIUM)

PEERDIR(
    yandex_io/libs/audio_player/mock
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/services/mediad/media/players/yandexmusic
    yandex_io/tests/testlib
)

SRCS(
    test_yandex_music.cpp
)

END()
