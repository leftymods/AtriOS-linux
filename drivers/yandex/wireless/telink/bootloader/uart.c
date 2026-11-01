#include "uart.h"

#include <inttypes.h>

#define DEBUG_UART 0

typedef struct {
    uint32_t length;
    uint8_t payload[256];
} UartRxBuffer;

typedef struct {
    uint8_t buffer[512];
    size_t head;
    size_t tail;
} UartData;

static UartRxBuffer rxBuffer = {};
static volatile UartData data = {};

static void receiveHandler() {
    DEBUG(DEBUG_UART, "UART: received %" PRIu32 " bytes\r\n", rxBuffer.length);

    for (size_t i = 0; i < rxBuffer.length; i++) {
        size_t newTail = (data.tail + 1) % sizeof(data.buffer);

        if (newTail == data.head) {
            DEBUG(DEBUG_UART, "UART: buffer overflow\r\n");
            break;
        }

        data.buffer[data.tail] = rxBuffer.payload[i];

        data.tail = newTail;
    }
}

void tlUartInit(uint32_t baudRate) {
    drv_uart_init(baudRate, (uint8_t*)&rxBuffer, sizeof(rxBuffer), receiveHandler);

    data.head = 0;
    data.tail = 0;
}

size_t tlUartAvailableBytes() {
    size_t available;

    if (data.head <= data.tail) {
        available = data.tail - data.head;
    } else {
        available = sizeof(data.buffer) - (data.head - data.tail);
    }

    return available;
}

bool tlUartReceive(uint8_t* buffer, size_t count) {
    if (tlUartAvailableBytes() < count) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        buffer[i] = data.buffer[data.head];

        data.head = (data.head + 1) % sizeof(data.buffer);
    }

    return true;
}

void tlUartSend(const uint8_t* buffer, size_t size) {
    drv_uart_tx_start((uint8_t*)buffer, size);
}

void tlUartSendByte(uint8_t byte) {
    tlUartSend(&byte, 1);
}

void tlUartFlush() {
    data.head = data.tail;
}
