OWNER(g:yandex_io)

UNITTEST_FOR(yandex_io/services/mediad)

SIZE(MEDIUM)

INCLUDE(${ARCADIA_ROOT}/yandex_io/tests/unittest.inc)

PEERDIR(
    yandex_io/interfaces/auth/mock 
    yandex_io/interfaces/device_state/mock 
    yandex_io/interfaces/multiroom/mock 
    yandex_io/libs/audio_player/mock
    yandex_io/libs/base
    yandex_io/libs/ipc
    yandex_io/libs/json_utils
    yandex_io/libs/telemetry/null
    yandex_io/services/mediad
    yandex_io/sdk
    yandex_io/tests/testlib
    yandex_io/tests/testlib/null_sdk
    yandex_io/tests/testlib/null_device_state_capability
)

SRCS(
    test_media_endpoint.cpp
)

REQUIREMENTS(ram:9)

END()
