#pragma once

#include "tl_common.h"
#include "zbhci.h"

typedef enum {
    ZBHCI_CMD_MFG_EUI64_REQ = 0x0800,
    ZBHCI_CMD_MFG_START_REQ = 0x0801,
    ZBHCI_CMD_MFG_STOP_REQ = 0x0802,
    ZBHCI_CMD_MFG_SET_CHANNEL_REQ = 0x0803,
    ZBHCI_CMD_MFG_SET_POWER_REQ = 0x0804,
    ZBHCI_CMD_MFG_SEND_PACKET_REQ = 0x0805,

    ZBHCI_CMD_MFG_EUI64_RSP = 0x8800,
    ZBHCI_CMD_MFG_RECEIVE_PACKET_IND = 0x8806,
} zbhci_mfgMsgType;

void zbhciMfgReceivePacket(uint8_t lqi, int8_t rssi, uint8_t length, uint8_t* payload);
