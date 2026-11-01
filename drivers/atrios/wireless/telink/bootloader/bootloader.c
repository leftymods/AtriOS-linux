#include "bootloader.h"
#include "crc.h"
#include "uart.h"
#include "xmodem.h"

#include <inttypes.h>

#define DEBUG_BOOTLOADER 1

#define TELINK_MAGIC 0x544c4e4b

#define PRIMARY_BOOT_ADDRESS 0x00000
#define ALTERNATE_BOOT_ADDRESS 0x40000

typedef enum {
    IMAGE_OK = 0,
    IMAGE_ERROR_HEADER = 1,
    IMAGE_ERROR_TOO_LARGE = 2,
    IMAGE_ERROR_TRUNCATED = 3,
    IMAGE_ERROR_CRC = 4,
} ImageError;

typedef struct {
    uint16_t start;
    uint16_t appVersion;
    uint16_t stackVersion;
    uint8_t padding0[12];
    uint16_t manufacturerCode;
    uint16_t type;
    uint8_t padding1[2];
    uint32_t size;
    uint8_t padding2[4];
    uint32_t magic;
} ImageHeader;

typedef struct {
    uint32_t crc32;
} ImageTrailer;

typedef struct {
    ImageError error;
    uint32_t offset;
    ImageHeader header;
    uint32_t crc32;
} Upload;

static uint32_t getUploadSize(Upload* state) {
    return state->header.size + sizeof(ImageTrailer);
}

static bool xmodemHandler(void* arg, uint8_t* data, size_t size) {
    Upload* state = (Upload*)arg;

    if (data == NULL) {
        if (!state->offset || state->offset < getUploadSize(state)) {
            DEBUG(DEBUG_BOOTLOADER, "BL: image was truncated\r\n");
            state->error = IMAGE_ERROR_TRUNCATED;
            return false;
        }

        ImageTrailer trailer;

        flash_read(BOOTLOADER_OTA_ADDRESS + state->header.size, sizeof(trailer), (uint8_t*)&trailer);

        if (state->crc32 != trailer.crc32) {
            DEBUG(DEBUG_BOOTLOADER, "BL: image CRC mismatch %08" PRIx32 " %08" PRIx32 "\r\n", state->crc32, trailer.crc32);
            state->error = IMAGE_ERROR_CRC;
            return false;
        }

        return true;
    }

    if (size != XMODEM_DATA_SIZE) {
        DEBUG(DEBUG_BOOTLOADER, "BL: wrong packet size\r\n");
        return false;
    }

    if (state->offset == 0) {
        memcpy(&state->header, data, sizeof(state->header));

        if (state->header.magic != TELINK_MAGIC) {
            DEBUG(DEBUG_BOOTLOADER, "BL: bad image\r\n");
            state->error = IMAGE_ERROR_HEADER;
            return false;
        }

        if (getUploadSize(state) > BOOTLOADER_OTA_MAX_SIZE) {
            DEBUG(DEBUG_BOOTLOADER, "BL: upload is too large\r\n");
            state->error = IMAGE_ERROR_TOO_LARGE;
            return false;
        }
    }

    if (state->offset >= getUploadSize(state)) {
        DEBUG(DEBUG_BOOTLOADER, "BL: extra data");
        return false;
    }

    size_t crcSize = size;

    if (state->offset + crcSize > state->header.size) {
        if (state->offset <= state->header.size) {
            crcSize = state->header.size - state->offset;
        } else {
            crcSize = 0;
        }
    }

    state->crc32 = tlCrc32(data, crcSize, state->crc32);

    if ((state->offset & (FLASH_SECTOR_SIZE - 1)) == 0) {
        DEBUG(DEBUG_BOOTLOADER, "BL: erasing flash at 0x%08" PRIx32 "\r\n", BOOTLOADER_OTA_ADDRESS + state->offset);
        flash_erase(BOOTLOADER_OTA_ADDRESS + state->offset);
    }

    DEBUG(DEBUG_BOOTLOADER, "BL: writing image at 0x%08" PRIx32 "\r\n", BOOTLOADER_OTA_ADDRESS + state->offset);
    flash_write(BOOTLOADER_OTA_ADDRESS + state->offset, size, data);

    state->offset += size;

    return true;
}

static void showMenu() {
    uint8_t menu[] =
        "\r\nYandex Bootloader vX.Y.ZZ"
        "\r\n1. upload ybl"
        "\r\n2. run"
        "\r\nBL > ";

    DEBUG(DEBUG_BOOTLOADER, "BL: menu\r\n");

    menu[21] = ((APP_RELEASE >> 4) & 0x0f) + '0';
    menu[23] = (APP_RELEASE & 0x0f) + '0';

    menu[25] = ((APP_BUILD >> 4) & 0x0f) + '0';
    menu[26] = (APP_BUILD & 0x0f) + '0';

    tlUartSend(menu, sizeof(menu));
}

static uint8_t nibbleToHex(uint8_t nibble) {
    return (nibble > 9) ? (nibble - 10 + 'A') : (nibble + '0');
}

static void upload() {
    static const uint8_t UPLOAD_START[] = "\r\nUpload start\r\n";
    static const uint8_t UPLOAD_COMPLETE[] = "\r\nUpload complete\r\n";
    static const uint8_t UPLOAD_ABORTED[] = "\r\nUpload aborted\r\n";
    static const uint8_t XMODEM_ERROR[] = "\r\nXmodem error 0x";
    static const uint8_t IMAGE_ERROR[] = "\r\nImage error 0x";

    Upload state = {};

    DEBUG(DEBUG_BOOTLOADER, "BL: upload\r\n");

    tlUartSend(UPLOAD_START, sizeof(UPLOAD_START) - 1);

    XmodemError xmodemError = tlXmodemReceive(xmodemHandler, &state);

    WaitMs(10);
    tlUartFlush();

    if (xmodemError == XMODEM_OK) {
        DEBUG(DEBUG_BOOTLOADER, "BL: upload complete\r\n");

        tlUartSend(UPLOAD_COMPLETE, sizeof(UPLOAD_COMPLETE) - 1);

        return;
    }

    DEBUG(DEBUG_BOOTLOADER, "BL: upload aborted\r\n");

    tlUartSend(UPLOAD_ABORTED, sizeof(UPLOAD_ABORTED) - 1);

    uint8_t errorCode;

    if (state.error != IMAGE_OK) {
        DEBUG(DEBUG_BOOTLOADER, "BL: image error 0x%02" PRIx8 "\r\n", state.error);

        tlUartSend(IMAGE_ERROR, sizeof(IMAGE_ERROR) - 1);

        errorCode = state.error;
    } else {
        DEBUG(DEBUG_BOOTLOADER, "BL: xmodem error 0x%02" PRIx8 "\r\n", xmodemError);

        tlUartSend(XMODEM_ERROR, sizeof(XMODEM_ERROR) - 1);

        errorCode = xmodemError;
    }

    tlUartSendByte(nibbleToHex(errorCode >> 4));
    tlUartSendByte(nibbleToHex(errorCode & 0x0f));
    tlUartSendByte('\r');
    tlUartSendByte('\n');
}

static void copyFlash(uint32_t sourceAddr, uint32_t destAddr, uint32_t length) {
    uint8_t buffer[FLASH_PAGE_SIZE];

    DEBUG(DEBUG_BOOTLOADER, "BL: copying 0x%08" PRIx32 " bytes from 0x%08" PRIx32 " to 0x%08" PRIx32 "\r\n", length, sourceAddr, destAddr);

    for (uint32_t i = 0; i < length; i += FLASH_PAGE_SIZE) {
        if ((i & (FLASH_SECTOR_SIZE - 1)) == 0) {
            DEBUG(DEBUG_BOOTLOADER, "BL: erasing flash at 0x%08" PRIx32 "\r\n", destAddr + i);
            flash_erase(destAddr + i);
        }

        // Copy the first page last
        if (i == 0) {
            continue;
        }

        DEBUG(DEBUG_BOOTLOADER, "BL: writing flash at 0x%08" PRIx32 "\r\n", destAddr + i);

        flash_read(sourceAddr + i, sizeof(buffer), buffer);
        flash_write(destAddr + i, sizeof(buffer), buffer);
    }

    // Copy the first page last
    if (length > 0) {
        DEBUG(DEBUG_BOOTLOADER, "BL: writing flash at 0x%08" PRIx32 "\r\n", destAddr);

        flash_read(sourceAddr, sizeof(buffer), buffer);
        flash_write(destAddr, sizeof(buffer), buffer);
    }
}

static void invalidateImage(uint32_t addr) {
    uint32_t magic = 0;

    DEBUG(DEBUG_BOOTLOADER, "BL: invalidating image at 0x%08" PRIx32 "\r\n", addr);

    flash_write(addr + FLASH_TLNK_FLAG_OFFSET, sizeof(magic), (uint8_t*)&magic);
}

static bool checkCrc32(uint32_t addr) {
    uint32_t length = FLASH_PAGE_SIZE;
    uint8_t buffer[FLASH_PAGE_SIZE];
    uint32_t crc32 = 0;
    ImageHeader header;
    ImageTrailer trailer;

    for (uint32_t i = 0; i < length; i += FLASH_PAGE_SIZE) {
        uint32_t chunkSize = FLASH_PAGE_SIZE;

        if (chunkSize > length - i) {
            chunkSize = length - i;
        }

        flash_read(addr + i, chunkSize, buffer);

        if (i == 0) {
            memcpy(&header, buffer, sizeof(header));
            length = header.size;
        }

        crc32 = tlCrc32(buffer, chunkSize, crc32);
    }

    flash_read(addr + length, sizeof(trailer), (uint8_t*)&trailer);

    return crc32 == trailer.crc32;
}

static void updateApp(const ImageHeader* header) {
    DEBUG(DEBUG_BOOTLOADER, "BL: updating app\r\n");

    if (header->size > BOOTLOADER_OTA_MAX_SIZE) {
        DEBUG(DEBUG_BOOTLOADER, "BL: app image is too large\r\n");
        return;
    }

    ImageHeader blHeader;

    flash_read(0, sizeof(blHeader), (uint8_t*)&blHeader);

    if (blHeader.magic != TELINK_MAGIC) {
        DEBUG(DEBUG_BOOTLOADER, "BL: bootloader update in progress, cannot update app\r\n");
        return;
    }

    copyFlash(BOOTLOADER_OTA_ADDRESS, BOOTLOADER_APP_ADDRESS, header->size);

    invalidateImage(BOOTLOADER_OTA_ADDRESS);

    DEBUG(DEBUG_BOOTLOADER, "BL: update complete\r\n");
}

static void updateBootloader(const ImageHeader* header) {
    DEBUG(DEBUG_BOOTLOADER, "BL: updating bootloader\r\n");

    if (header->size > BOOTLOADER_APP_ADDRESS) {
        DEBUG(DEBUG_BOOTLOADER, "BL: bootloader image is too large\r\n");
        return;
    }

    if (!checkCrc32(BOOTLOADER_OTA_ADDRESS)) {
        DEBUG(DEBUG_BOOTLOADER, "BL: bootloader image has bad CRC32\r\n");
        return;
    }

    ImageHeader blHeader;

    flash_read(PRIMARY_BOOT_ADDRESS, sizeof(blHeader), (uint8_t*)&blHeader);

    if (blHeader.magic == TELINK_MAGIC) {
        DEBUG(DEBUG_BOOTLOADER, "BL: copying self to alternate boot address\r\n");

        invalidateImage(BOOTLOADER_APP_ADDRESS);

        copyFlash(PRIMARY_BOOT_ADDRESS, ALTERNATE_BOOT_ADDRESS, blHeader.size);

        invalidateImage(PRIMARY_BOOT_ADDRESS);

        DEBUG(DEBUG_BOOTLOADER, "BL: rebooting\r\n");

        SYSTEM_RESET();
    } else {
        DEBUG(DEBUG_BOOTLOADER, "BL: copying new image to primary boot address\r\n");

        copyFlash(BOOTLOADER_OTA_ADDRESS, PRIMARY_BOOT_ADDRESS, header->size);

        invalidateImage(BOOTLOADER_OTA_ADDRESS);

        DEBUG(DEBUG_BOOTLOADER, "BL: rebooting\r\n");

        SYSTEM_RESET();
    }
}

static void update() {
    ImageHeader header;

    DEBUG(DEBUG_BOOTLOADER, "BL: update\r\n");

    flash_read(BOOTLOADER_OTA_ADDRESS, sizeof(header), (uint8_t*)&header);

    if (header.magic != TELINK_MAGIC) {
        DEBUG(DEBUG_BOOTLOADER, "BL: no update\r\n");
        return;
    }

    DEBUG(DEBUG_BOOTLOADER, "BL: update appVersion 0x%04" PRIx16 "\r\n", header.appVersion);
    DEBUG(DEBUG_BOOTLOADER, "BL: update stackVersion 0x%04" PRIx16 "\r\n", header.stackVersion);
    DEBUG(DEBUG_BOOTLOADER, "BL: update manufacturerCode 0x%04" PRIx16 "\r\n", header.manufacturerCode);
    DEBUG(DEBUG_BOOTLOADER, "BL: update type 0x%04" PRIx16 "\r\n", header.type);
    DEBUG(DEBUG_BOOTLOADER, "BL: update size %" PRIu32 "\r\n", header.size);

    if (header.type & 0xff == IMAGE_TYPE_BOOTLOADER) {
        updateBootloader(&header);
    } else {
        updateApp(&header);
    }
}

static void boot(BootloaderCallback run) {
    static const uint8_t BOOT_ERROR[] = "\r\nBoot error\r\n";

    ImageHeader header;

    DEBUG(DEBUG_BOOTLOADER, "BL: boot\r\n");

    flash_read(BOOTLOADER_APP_ADDRESS, sizeof(header), (uint8_t*)&header);

    if (header.magic == TELINK_MAGIC) {
        run();

        DEBUG(DEBUG_BOOTLOADER, "BL: boot failed\r\n");
    } else {
        DEBUG(DEBUG_BOOTLOADER, "BL: no image\r\n");
    }

    tlUartSend(BOOT_ERROR, sizeof(BOOT_ERROR) - 1);
}

void tlBootloaderMain(bool bootToApp, BootloaderCallback run, const bool* stop) {
    DEBUG(DEBUG_BOOTLOADER, "BL: start\r\n");

    if (bootToApp) {
        update();
        boot(run);
    }

    while (true) {
        uint8_t action;

        showMenu();

        while (!tlUartReceive(&action, 1)) {
            if (stop && *stop) {
                return;
            }
        }

        switch (action) {
            case '1':
                upload();
                break;
            case '2':
                update();
                boot(run);
                break;
            default:
                DEBUG(DEBUG_BOOTLOADER, "BL: unexpected input\r\n");
                break;
        }
    }
}
