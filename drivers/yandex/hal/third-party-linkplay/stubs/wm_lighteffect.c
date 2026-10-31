#include <stdlib.h>

#define STUB_FN(name) void name() { abort(); }

STUB_FN(light_init);
STUB_FN(light_show);
