OWNER(g:yandex_io)

RECURSE(
    common
    reader
    tinyalsa
    writer
)

IF (OS_LINUX)
    RECURSE(
        alsa
    )
ENDIF()
