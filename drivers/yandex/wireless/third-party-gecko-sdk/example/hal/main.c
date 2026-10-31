#include PLATFORM_HEADER
#include "hal/hal.h"
#include "hal/plugin/serial/serial.h"

void halNvm3Callback(Ecode_t status) {
    (void)status;
}

void halRadioPowerUpHandler(void) {
}

void halRadioPowerDownHandler(void) {
}

int main(void) {
    halInit();

    char buffer[256];

    while (1) {
        emberSerialReadLine(comPortUsart0, buffer, 255);

        if (strncmp(buffer, "BOOTLOADER", sizeof(buffer)) == 0) {
            halLaunchStandaloneBootloader(STANDALONE_BOOTLOADER_NORMAL_MODE);
        } else if (strncmp(buffer, "RANDOM", sizeof(buffer)) == 0) {
            emberSerialPrintf(comPortUsart0, "%u\r\n", halCommonGetRandom());
        } else {
            emberSerialPrintf(comPortUsart0, "Unknown command '%s'\r\n", buffer);
        }
    }
}
