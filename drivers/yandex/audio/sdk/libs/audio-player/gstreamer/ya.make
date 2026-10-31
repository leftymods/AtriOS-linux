OWNER(g:yandex_io)

LIBRARY()

PEERDIR(
    contrib/deprecated/jsoncpp
    contrib/restricted/spdlog
    yandex_io/libs/audio_player/base
    yandex_io/libs/errno
    yandex_io/libs/threading
    yandex_io/libs/logging
)

# NOTE: even though glib/gstreamer headers are used, none of their libs are directly used via PEERDIR.
# Only our proxy below MUST be used to link with g* libs at all circumstances.
PEERDIR(
    yandex_io/oss_libs/gstreamer
)

IF (OS_ANDROID)
    CFLAGS(-DQUASAR_GSTREAMER_OPENSLES)
ENDIF()

IF (OS_LINUX)
    CFLAGS(-DQUASAR_GSTREAMER_ALSA)
ENDIF()

DEFAULT(USE_GSTREAMER_LIBAV yes)
IF (USE_GSTREAMER_LIBAV)
    CFLAGS(-DQUASAR_GSTREAMER_LIBAV)
ENDIF()

SRCS(
    gio_logging_resolver.cpp
    gstreamer.cpp
    gstreamer_audio_clock.cpp
    gstreamer_audio_player.cpp
    gstreamer_time_provider.cpp
)

END()

RECURSE_FOR_TESTS(tests)
