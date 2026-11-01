#include "mfg.h"
#include "hci.h"
#include "zb_task_queue.h"

#define MFG_CHANNEL_START 11
#define MFG_CHANNEL_STOP 26

#define MFG_CHANNEL_TO_PHYSICAL(channel) ((channel - 10) * 5)

#define MFG_TX_BUFFER_SIZE (ZB_RADIO_TX_HDR_LEN + 127)

#define MFG_RX_BUFFER_SIZE 144
#define MFG_RX_BUFFER_COUNT 32

uint8_t mfgChannel = MFG_CHANNEL_START;

__attribute__((aligned(4))) uint8_t mfgTxBuffer[MFG_TX_BUFFER_SIZE];

MEMPOOL_DECLARE(mfgRxPool, mfgRxPoolMem, MFG_RX_BUFFER_SIZE, MFG_RX_BUFFER_COUNT);
uint8_t* mfgRxBuffer = NULL;

static void mfgReceivePacket(void* arg);

void rf_tx_irq_handler() {
    ZB_RADIO_TRX_SWITCH(RF_MODE_RX, MFG_CHANNEL_TO_PHYSICAL(mfgChannel));
}

void rf_rx_irq_handler() {
    uint8_t* packet = mfgRxBuffer;

    if (!ZB_RADIO_CRC_OK(packet)) {
        ZB_RADIO_RX_BUF_CLEAR(mfgRxBuffer);
        ZB_RADIO_RX_ENABLE;
        return;
    }

    if (!ZB_RADIO_PACKET_LENGTH_OK(packet)) {
        ZB_RADIO_RX_BUF_CLEAR(mfgRxBuffer);
        ZB_RADIO_RX_ENABLE;
        return;
    }

    uint8_t* nextBuffer = mempool_alloc(&mfgRxPool);
    if (!nextBuffer) {
        ZB_RADIO_RX_BUF_CLEAR(mfgRxBuffer);
        ZB_RADIO_RX_ENABLE;
        return;
    }

    mfgRxBuffer = nextBuffer;
    ZB_RADIO_RX_BUF_CLEAR(mfgRxBuffer);
    ZB_RADIO_RX_BUF_SET(mfgRxBuffer);
    ZB_RADIO_RX_ENABLE;

    TL_SCHEDULE_TASK(mfgReceivePacket, packet);
}

void mfgInit() {
    mempool_init(&mfgRxPool, &mfgRxPoolMem, MFG_RX_BUFFER_SIZE, MFG_RX_BUFFER_COUNT);

    ZB_RADIO_TX_POWER_SET(RF_POWER_INDEX_P9p11dBm);
    ZB_RADIO_TRX_CFG(MFG_TX_BUFFER_SIZE);

    mfgRxBuffer = mempool_alloc(&mfgRxPool);

    ZB_RADIO_RX_BUF_CLEAR(mfgRxBuffer);
    ZB_RADIO_RX_BUF_SET(mfgRxBuffer);
}

void mfgStart() {
    ZB_RADIO_TRX_SWITCH(RF_MODE_RX, MFG_CHANNEL_TO_PHYSICAL(mfgChannel));
}

void mfgStop() {
    ZB_RADIO_TRX_SWITCH(RF_MODE_OFF, MFG_CHANNEL_TO_PHYSICAL(mfgChannel));
}

void mfgSetChannel(uint8_t channel) {
    if (channel < MFG_CHANNEL_START || channel > MFG_CHANNEL_STOP) {
        return;
    }

    mfgChannel = channel;

    uint32_t r = drv_disable_irq();

    uint8_t status = ZB_RADIO_TRX_STA_GET();

    ZB_RADIO_TRX_SWITCH(status, MFG_CHANNEL_TO_PHYSICAL(mfgChannel));

    drv_restore_irq(r);
}

void mfgSetPower(int8_t power) {
    (void)power; // TODO
}

void mfgSendPacket(const uint8_t* data, uint8_t length) {
    ZB_RADIO_DMA_HDR_BUILD(mfgTxBuffer, length);
    mfgTxBuffer[4] = length + 2;
    memcpy(mfgTxBuffer + ZB_RADIO_TX_HDR_LEN, data, length);

    ZB_RADIO_TRX_SWITCH(RF_MODE_TX, MFG_CHANNEL_TO_PHYSICAL(mfgChannel));
    WaitUs(ZB_TX_WAIT_US);

    ZB_RADIO_TX_DONE_CLR;
    ZB_RADIO_RX_DONE_CLR;

    ZB_RADIO_TX_START(mfgTxBuffer);
}

static void mfgReceivePacket(void* arg) {
    uint8_t* packet = arg;

    uint8_t* payload = packet + ZB_RADIO_RX_HDR_LEN;
    uint8_t length = ZB_RADIO_ACTUAL_PAYLOAD_LEN(packet);

    int8_t rssi = ZB_RADION_PKT_RSSI_GET(packet) - 110;
    uint8_t lqi = 0;
    ZB_RADIO_RSSI_TO_LQI(0, rssi, lqi);

    zbhciMfgReceivePacket(lqi, rssi, length, payload);

    mempool_free(&mfgRxPool, arg);
}
