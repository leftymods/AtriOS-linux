#include <smart_devices/libs/telink/bootloader/bootloader.h>
#include <smart_devices/libs/telink/bootloader/uart.h>

#include "tl_common.h"

#define BOOT_FLAG 0xa5

static void boot() {
    if (analog_read_reg8(PM_ANA_REG_BOOT_FLAG) == BOOT_FLAG) {
        analog_write_reg8(PM_ANA_REG_BOOT_FLAG, 0x00);

        ((void (*)())(FLASH_R_BASE_ADDR + FLASH_ADDR_OF_APP_FW))();
    }
}

static void run() {
    analog_write_reg8(PM_ANA_REG_BOOT_FLAG, BOOT_FLAG);

    SYSTEM_RESET();
}

int main() {
    drv_platform_init();

    boot();

    drv_uart_pin_set(UART_TX_PIN, UART_RX_PIN);

    ev_buf_init();

    tlUartInit(UART_BAUDRATE);

    drv_enable_irq();

    bool bootToApp = drv_gpio_read(BOOT_PIN);

    tlBootloaderMain(bootToApp, run, NULL);

    return 0;
}
