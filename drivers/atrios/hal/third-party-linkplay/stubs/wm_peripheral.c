#include <stdlib.h>

#define STUB_FN(name) void name() { abort(); }

STUB_FN(Peri_Init);
STUB_FN(Peri_LED_Animation);
STUB_FN(Peri_MCU_Send);
