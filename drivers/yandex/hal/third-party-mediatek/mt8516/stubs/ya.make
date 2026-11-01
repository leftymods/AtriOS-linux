OWNER(g:yandex_io)

LIBRARY()

NO_UTIL()

MESSAGE(Using stubs for MediaTek bluetooth libs)
IF (YANDEXIO_PACKAGE_BUILD)
    MESSAGE(FATAL_ERROR Cannot use stubs in a real package)
ENDIF()

SRCS(mtk_bt_service_client.c)

END()
