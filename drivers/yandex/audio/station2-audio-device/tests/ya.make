OWNER(
    g:yandex_io
    ohmikey
)

UNITTEST_FOR(smart_devices/platforms/yandexstation_2/platformd/audio_device)

SRCS(
    test_audio_readers.cpp
    test_tinyalsa_mixer.cpp
    test_yandex_station2_audio_device.cpp
)

PEERDIR(
    yandex_io/libs/telemetry/mock
    yandex_io/libs/telemetry/null
    yandex_io/modules/audio_input/vqe/controller
    yandex_io/modules/audio_input/vqe/engine
    yandex_io/tests/testlib/unittest_helper
)

END()
