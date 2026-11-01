#include <stdlib.h>

#define STUB_FN(name) void name() { abort(); }

STUB_FN(linkPlay_resample);
STUB_FN(linkplay_resample_instance_create);
STUB_FN(linkplay_resample_uninit);
STUB_FN(linkplay_set_resample_format);
