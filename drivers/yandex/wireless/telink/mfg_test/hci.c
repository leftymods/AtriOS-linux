#include "hci.h"
#include "mfg.h"
#include "zb_common.h"

#define ZBHCI_MSG_PAYLOAD_LEN (64 - ZBHCI_MSG_HDR_LEN)

static void zbhci_mfgCmdHandler(void* arg) {
    const zbhci_cmdHandler_t* cmdInfo = arg;

    uint16_t cmdId = cmdInfo->cmdId;
    const uint8_t* payload = cmdInfo->payload;

    switch (cmdId) {
        case ZBHCI_CMD_MFG_EUI64_REQ: {
            uint8_t eui64[8];
            uint8_t response[8];

            flash_read(CFG_MAC_ADDRESS, sizeof(eui64), eui64);

            ZB_IEEE_ADDR_REVERT(response, eui64);

            zbhciTx(ZBHCI_CMD_MFG_EUI64_RSP, sizeof(response), response);

            break;
        }
        case ZBHCI_CMD_MFG_START_REQ:
            mfgStart();
            break;
        case ZBHCI_CMD_MFG_STOP_REQ:
            mfgStop();
            break;
        case ZBHCI_CMD_MFG_SET_CHANNEL_REQ:
            mfgSetChannel(payload[0]);
            break;
        case ZBHCI_CMD_MFG_SET_POWER_REQ:
            mfgSetPower(payload[0]);
            break;
        case ZBHCI_CMD_MFG_SEND_PACKET_REQ: {
            uint8_t length = payload[0];
            const uint8_t* data = &payload[1];

            mfgSendPacket(data, length);

            break;
        }
        default:
            break;
    }

    ev_buf_free(arg);
}

void zbhciMfgReceivePacket(uint8_t lqi, int8_t rssi, uint8_t length, uint8_t* payload) {
    uint8_t data[ZBHCI_MSG_PAYLOAD_LEN];
    uint8_t* p = data;

    *p++ = lqi;
    *p++ = rssi;
    *p++ = length;

    length = min(length, sizeof(data) - 3);
    memcpy(p, payload, length);

    zbhciTx(ZBHCI_CMD_MFG_RECEIVE_PACKET_IND, 3 + length, data);
}

void zbhciCmdHandler(uint16_t msgType, uint16_t msgLen, uint8_t* p) {
    uint8_t ret[4];
    uint8_t status = ZBHCI_MSG_STATUS_SUCCESS;

    zbhci_cmdHandler_t* cmdInfo = (zbhci_cmdHandler_t*)ev_buf_allocate(msgLen + 4);

    if (cmdInfo) {
        cmdInfo->cmdId = msgType;
        memcpy(cmdInfo->payload, p, msgLen);

        switch (msgType) {
            case ZBHCI_CMD_MFG_EUI64_REQ:
            case ZBHCI_CMD_MFG_START_REQ:
            case ZBHCI_CMD_MFG_STOP_REQ:
            case ZBHCI_CMD_MFG_SET_CHANNEL_REQ:
            case ZBHCI_CMD_MFG_SET_POWER_REQ:
            case ZBHCI_CMD_MFG_SEND_PACKET_REQ:
                TL_SCHEDULE_TASK(zbhci_mfgCmdHandler, cmdInfo);
                break;
            default:
                ev_buf_free((uint8_t*)cmdInfo);
                status = ZBHCI_MSG_STATUS_UNHANDLED_COMMAND;
                break;
        }
    } else {
        status = ZBHCI_MSG_STATUS_NO_MEMORY;
    }

    ret[0] = (msgType >> 8) & 0xff;
    ret[1] = msgType & 0xff;
    ret[2] = status;
    ret[3] = 0;

    zbhciTx(ZBHCI_CMD_ACKNOWLEDGE, 4, ret);
}
