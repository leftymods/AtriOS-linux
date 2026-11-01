#pragma once

#include "tl_common.h"

#if defined(__cplusplus)
extern "C" {
#endif

    void tlUartInit(uint32_t baudRate);

    size_t tlUartAvailableBytes();

    bool tlUartReceive(uint8_t* buffer, size_t count);

    void tlUartSend(const uint8_t* buffer, size_t size);

    void tlUartSendByte(uint8_t byte);

    void tlUartFlush();

#if defined(__cplusplus)
}
#endif
