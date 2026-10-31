#include "crc.h"
#include "uart.h"
#include "xmodem.h"

#define DEBUG_XMODEM 1

typedef enum {
    XMODEM_CMD_SOH = 0x01,
    XMODEM_CMD_EOT = 0x04,
    XMODEM_CMD_ACK = 0x06,
    XMODEM_CMD_NAK = 0x15,
    XMODEM_CMD_CAN = 0x18,
    XMODEM_CMD_C = 0x43
} XmodemCmd;

#define XMODEM_MAX_RETRIES 10

typedef struct {
    uint8_t cmd;
    uint8_t packetNumber;
    uint8_t packetNumberInv;
    uint8_t data[XMODEM_DATA_SIZE];
    uint8_t crcH;
    uint8_t crcL;
} XmodemPacket;

static void reply(uint8_t cmd) {
    DEBUG(DEBUG_XMODEM, "XMODEM: reply %02x\r\n", cmd);

    tlUartSendByte(cmd);

    if (cmd == XMODEM_CMD_CAN) {
        tlUartSendByte(cmd);
        tlUartSendByte(cmd);
    }
}

static void flush() {
    DEBUG(DEBUG_XMODEM, "XMODEM: flush\r\n");

    uint32_t time = clock_time();

    while (!clock_time_exceed(time, 10 * 1000)) {
        if (tlUartAvailableBytes() > 0) {
            tlUartFlush();
            time = clock_time();
        }
    }
}

static bool receivePacket(XmodemPacket* packet) {
    uint8_t* buffer = (uint8_t*)packet;
    uint32_t time = clock_time();

    while (!tlUartReceive(buffer, 1)) {
        if (clock_time_exceed(time, 1000 * 1000)) {
            return false;
        }
    }

    if (packet->cmd != XMODEM_CMD_SOH) {
        return true;
    }

    while (!tlUartReceive(buffer + 1, sizeof(XmodemPacket) - 1)) {
        if (clock_time_exceed(time, 3000 * 1000)) {
            return false;
        }
    }

    return true;
}

static bool verifyPacket(XmodemPacket* packet, uint8_t packetNumber, XmodemError* error) {
    if (packet->packetNumber + packet->packetNumberInv != 0xff) {
        DEBUG(DEBUG_XMODEM, "XMODEM: packet number error\r\n");
        *error = XMODEM_ERROR_PACKET_NUMBER;
        return false;
    }

    uint16_t crc16 = tlCrc16(packet->data, XMODEM_DATA_SIZE, 0);

    if (packet->crcH != ((crc16 >> 8) & 0xff)) {
        DEBUG(DEBUG_XMODEM, "XMODEM: CRC error\r\n");
        *error = XMODEM_ERROR_CRC;
        return false;
    }

    if (packet->crcL != (crc16 & 0xff)) {
        DEBUG(DEBUG_XMODEM, "XMODEM: CRC error\r\n");
        *error = XMODEM_ERROR_CRC;
        return false;
    }

    if (packet->packetNumber == packetNumber) {
        DEBUG(DEBUG_XMODEM, "XMODEM: retransmit\r\n");
        *error = XMODEM_ERROR_RETRANSMIT;
        return true;
    }

    if (packet->packetNumber != (uint8_t)(packetNumber + 1)) {
        DEBUG(DEBUG_XMODEM, "XMODEM: unexpected packet number\r\n");
        *error = XMODEM_ERROR_PACKET_SEQUENCE;
        return false;
    }

    return true;
}

XmodemError tlXmodemReceive(XmodemCallback callback, void* arg) {
    XmodemPacket packet;
    uint8_t packetNumber = 0;
    int retries = 0;
    XmodemError error = XMODEM_OK;

    flush();
    reply(XMODEM_CMD_C);

    while (retries++ < XMODEM_MAX_RETRIES) {
        if (!receivePacket(&packet)) {
            DEBUG(DEBUG_XMODEM, "XMODEM: timeout\r\n");
            error = XMODEM_ERROR_TIMEOUT;
            flush();
            reply(packetNumber ? XMODEM_CMD_NAK : XMODEM_CMD_C);
            continue;
        }

        switch (packet.cmd) {
            case XMODEM_CMD_SOH:
                DEBUG(DEBUG_XMODEM, "XMODEM: SOH\r\n");
                break;
            case XMODEM_CMD_EOT:
                DEBUG(DEBUG_XMODEM, "XMODEM: EOT\r\n");

                if (callback(arg, NULL, 0)) {
                    reply(XMODEM_CMD_ACK);
                    return XMODEM_OK;
                } else {
                    reply(XMODEM_CMD_CAN);
                    return XMODEM_ERROR_CANCEL;
                }
            case XMODEM_CMD_CAN:
                DEBUG(DEBUG_XMODEM, "XMODEM: cancel\r\n");
                reply(XMODEM_CMD_CAN);
                return XMODEM_ERROR_CANCEL;
            default:
                DEBUG(DEBUG_XMODEM, "XMODEM: unknown cmd\r\n");
                reply(XMODEM_CMD_CAN);
                return XMODEM_ERROR_CMD;
        }

        if (!verifyPacket(&packet, packetNumber, &error)) {
            flush();
            reply(XMODEM_CMD_NAK);
            continue;
        }

        if (packet.packetNumber == (uint8_t)(packetNumber + 1)) {
            DEBUG(DEBUG_XMODEM, "XMODEM: packet %u\r\n", packet.packetNumber);

            packetNumber = packet.packetNumber;
            retries = 0;

            if (!callback(arg, packet.data, XMODEM_DATA_SIZE)) {
                DEBUG(DEBUG_XMODEM, "XMODEM: cancel\r\n");
                reply(XMODEM_CMD_CAN);
                return XMODEM_ERROR_CANCEL;
            }
        }

        reply(XMODEM_CMD_ACK);
    }

    DEBUG(DEBUG_XMODEM, "XMODEM: retry limit exceeded\r\n");
    reply(XMODEM_CMD_CAN);
    return error;
}
