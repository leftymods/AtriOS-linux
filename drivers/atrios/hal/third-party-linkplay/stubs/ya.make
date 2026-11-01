OWNER(g:yandex_io)

LIBRARY()

NO_UTIL()

MESSAGE(Using stubs for Linkplay platform libs)
IF (YANDEXIO_PACKAGE_BUILD)
    MESSAGE(FATAL_ERROR Cannot use stubs in a real package)
ENDIF()

SRCS(
    lpresample.c
    soft_prep.c
    wmdb.c
    wm_lighteffect.c
    wm_peripheral.c
)

END()
