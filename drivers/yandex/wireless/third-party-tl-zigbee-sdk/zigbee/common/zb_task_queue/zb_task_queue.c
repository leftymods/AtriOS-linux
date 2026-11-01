#include "zb_buffer.h"

bool is_zb_buf(void* p) {
    (void)p;

    return false;
}

u8 zb_buf_free(zb_buf_t* buf) {
    (void)buf;

    return 0;
}

void tl_zbMacTaskProc(void) {
}

void tl_zbNwkTaskProc(void) {
}

void zdo_ssInfoUpdate(void) {
}
