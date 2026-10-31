OWNER(g:yandex_io)

LIBRARY()

NO_UTIL()
NO_CLANG_TIDY()

# NOTE: this is a header-only library.
# An actual implementation (either stubs or shared libs) must also be linked
# Implementations for Linkplay-based platforms must be placed like `smart_devices/platforms/${name}/linkplay/`

END()
