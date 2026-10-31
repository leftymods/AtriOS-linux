#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define APP_RELEASE 0x10
#define APP_BUILD 0x01

#define IMAGE_TYPE_BOOTLOADER 0xff

#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096

#define FLASH_TLNK_FLAG_OFFSET 32

#define NV_BASE_ADDRESS 0xe6000

#define FLASH_ADDR_OF_APP_FW 0x8000
#define FLASH_ADDR_OF_OTA_IMAGE 0x77000
#define FLASH_OTA_IMAGE_MAX_SIZE 444 * 1024

#define UART_BAUDRATE 115200

#define DEBUG(flag, ...)                         \
    {                                            \
        char msg[1024];                          \
        snprintf(msg, sizeof(msg), __VA_ARGS__); \
        logMessage(msg);                         \
    }

#define SYSTEM_RESET() sys_reboot()

#if defined(__cplusplus)
extern "C" {
#endif

    void logMessage(const char* msg);

    void WaitMs(uint32_t ms);

    uint32_t clock_time();
    bool clock_time_exceed(uint32_t ref, uint32_t us);

    void drv_uart_init(uint32_t baudRate, uint8_t* rxBuf, uint16_t rxBufLen, void (*uartRecvCb)());
    uint8_t drv_uart_tx_start(uint8_t* data, uint32_t len);

    void flash_write(uint32_t addr, uint32_t len, uint8_t* buf);
    void flash_read(uint32_t addr, uint32_t len, uint8_t* buf);
    void flash_erase(uint32_t addr);

    void sys_reboot();

#if defined(__cplusplus)
}
#endif
