OWNER(g:yandex_io)

LIBRARY()
NO_CLANG_TIDY()

IF (OS_ANDROID)
    EXTRALIBS(log)
ENDIF()

NO_COMPILER_WARNINGS()

SRCS(
    tuya_smart_link.cpp
)

END()
