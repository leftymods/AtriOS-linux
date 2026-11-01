LIBRARY()

OWNER(g:yandex_io)

PEERDIR(
    contrib/libs/liquidfun

    smart_devices/libs/spectrogram 

    yandex_io/libs/json_utils
    yandex_io/libs/logging
)

ADDINCL(
    contrib/libs/liquidfun
)

SRCS(
    bricks_spectrogram.cpp
    fluid_spectrogram.cpp
    pads_spectrogram.cpp
    random_spectrogram.cpp
    spectrogram.cpp
    spectrogram_factory.cpp
    wave_spectrogram.cpp
)

END()
