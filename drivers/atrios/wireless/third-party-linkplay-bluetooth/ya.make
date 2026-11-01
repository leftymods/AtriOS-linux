OWNER(g:yandex_io)

LIBRARY()

NO_CLANG_TIDY()

IF (ARCH_ARM7)
    FROM_SANDBOX(1475707433 OUT libbsa.a liblinkplaybt.a)
ELSE()
    MESSAGE(Using stubs for Linkplay bluetooth libs)
    IF (YANDEXIO_PACKAGE_BUILD)
        MESSAGE(FATAL_ERROR Cannot use stubs in a real package)
    ENDIF()

    SRCS(
        stubs/linkplaybt.c
    )
ENDIF()

END()
