PY3_PROGRAM(lag_finder)

OWNER(g:yandex_io)

PY_SRCS(
    MAIN lag_finder.py
)

PEERDIR(
    voicetech/vqe/pylibs/audio_lag
    contrib/python/matplotlib
    contrib/python/numpy
    contrib/python/scipy
)

NO_CHECK_IMPORTS(matplotlib.*)

END()
