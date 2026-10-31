#pragma once

#include "version_cfg.h"

#define ZBHCI_UART 1
#define ZBHCI_EN 1

#if defined(MCU_CORE_B91)
    #define FLASH_CAP_SIZE_1M 1
    #define CLOCK_SYS_CLOCK_HZ 48000000
#else
    #error "MCU is undefined!"
#endif

#include "board.h"

typedef enum {
    EV_POLL_MAX,
} ev_poll_e;
