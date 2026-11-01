OWNER(g:yandex_io)

LIBRARY()

PYTHON(
    generate_secret_source.py
    --key development.pem
    --dest platform_key.cpp
    --digest platform_key.sha256
    IN development.pem
    OUT platform_key.cpp
    OUT_NOAUTO platform_key.sha256
)

END()

RECURSE(digest)

RECURSE_FOR_TESTS(tests)
