OWNER(g:yandex_io)

LIBRARY()

SRCS(
    channels_splitter.cpp
)

REQUIREMENTS(ram:10)

END()

RECURSE_FOR_TESTS(
    tests
)
