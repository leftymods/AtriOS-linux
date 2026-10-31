#pragma once

#ifdef __cplusplus
extern "C" {
#endif

    int xmodemReceive(int fd, unsigned char* dest, int destsz);
    int xmodemTransmit(int fd, const unsigned char* src, int srcsz);

#ifdef __cplusplus
}
#endif
