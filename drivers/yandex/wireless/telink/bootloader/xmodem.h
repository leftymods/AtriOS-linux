#pragma once

#include "tl_common.h"

#define XMODEM_DATA_SIZE 128

#if defined(__cplusplus)
extern "C" {
#endif

    typedef enum {
        XMODEM_OK = 0,
        XMODEM_ERROR_CANCEL = 1,
        XMODEM_ERROR_TIMEOUT = 2,
        XMODEM_ERROR_CMD = 3,
        XMODEM_ERROR_PACKET_NUMBER = 4,
        XMODEM_ERROR_PACKET_SEQUENCE = 5,
        XMODEM_ERROR_CRC = 6,
        XMODEM_ERROR_RETRANSMIT = 7
    } XmodemError;

    typedef bool (*XmodemCallback)(void* arg, uint8_t* data, size_t size);

    XmodemError tlXmodemReceive(XmodemCallback callback, void* arg);

#if defined(__cplusplus)
}
#endif
