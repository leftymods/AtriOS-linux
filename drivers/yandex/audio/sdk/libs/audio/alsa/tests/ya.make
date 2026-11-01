OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/libs/audio/alsa)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

SRCS(
    test_alsa_error.cpp
)

END()
