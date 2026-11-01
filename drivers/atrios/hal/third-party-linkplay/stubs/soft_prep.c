#include <stdlib.h>

#define STUB_FN(name) void name() { abort(); }

STUB_FN(Soft_Prep_Deinit);
STUB_FN(Soft_Prep_GetAudioParam);
STUB_FN(Soft_Prep_GetCapability);
STUB_FN(Soft_Prep_Init);
STUB_FN(Soft_Prep_StartCapture);
STUB_FN(Soft_Prep_StopCapture);
