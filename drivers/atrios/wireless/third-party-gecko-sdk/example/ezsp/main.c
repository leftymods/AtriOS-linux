#include PLATFORM_HEADER
#include <smart_devices/third_party/gecko_sdk/platform/base/hal/hal.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp/ezsp-protocol.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp/ezsp.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/ezsp-host/ash/ash-host.h>

#include <stdio.h>

void ezspErrorHandler(EzspStatus status) {
    (void)status;
}

void ezspStackStatusHandler(EmberStatus status) {
    (void)status;
}

void ezspNetworkFoundHandler(
    EmberZigbeeNetwork* networkFound,
    uint8_t lastHopLqi,
    int8_t lastHopRssi
) {
    (void)networkFound;
    (void)lastHopLqi;
    (void)lastHopRssi;
}

void ezspScanCompleteHandler(uint8_t channel, EmberStatus status) {
    (void)channel;
    (void)status;
}

void ezspUnusedPanIdFoundHandler(EmberPanId panId, uint8_t channel) {
    (void)panId;
    (void)channel;
}

void ezspChildJoinHandler(
    uint8_t index,
    bool joining,
    EmberNodeId childId,
    EmberEUI64 childEui64,
    EmberNodeType childType
) {
    (void)index;
    (void)joining;
    (void)childId;
    (void)childEui64;
    (void)childType;
}

void ezspMessageSentHandler(
    EmberOutgoingMessageType type,
    uint16_t indexOrDestination,
    EmberApsFrame* apsFrame,
    uint8_t messageTag,
    EmberStatus status,
    uint8_t messageLength,
    uint8_t* messageContents
) {
    (void)type;
    (void)indexOrDestination;
    (void)apsFrame;
    (void)messageTag;
    (void)status;
    (void)messageLength;
    (void)messageContents;
}

void ezspPollHandler(
    EmberNodeId childId
) {
    (void)childId;
}

void ezspIncomingMessageHandler(
    EmberIncomingMessageType type,
    EmberApsFrame* apsFrame,
    uint8_t lastHopLqi,
    int8_t lastHopRssi,
    EmberNodeId sender,
    uint8_t bindingIndex,
    uint8_t addressIndex,
    uint8_t messageLength,
    uint8_t* messageContents
) {
    (void)type;
    (void)apsFrame;
    (void)lastHopLqi;
    (void)lastHopRssi;
    (void)sender;
    (void)bindingIndex;
    (void)addressIndex;
    (void)messageLength;
    (void)messageContents;
}

void ezspTrustCenterJoinHandler(
    EmberNodeId newNodeId,
    EmberEUI64 newNodeEui64,
    EmberDeviceUpdate status,
    EmberJoinDecision policyDecision,
    EmberNodeId parentOfNewNodeId
) {
    (void)newNodeId;
    (void)newNodeEui64;
    (void)status;
    (void)policyDecision;
    (void)parentOfNewNodeId;
}

void ezspMfglibRxHandler(
    uint8_t linkQuality,
    int8_t rssi,
    uint8_t packetLength,
    uint8_t *packetContents
) {
    (void)linkQuality;
    (void)rssi;
    (void)packetLength;
    (void)packetContents;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return -1;
    }

    const char* serialPort = argv[1];

    halInit();

    strncpy(ashHostConfig.serialPort, serialPort, ASH_PORT_LEN);
    
    EzspStatus status = ezspInit();

    if (status != EZSP_SUCCESS) {
        printf("ezspInit 0x%X\n", status);
        return -1;
    }

    uint8_t ncpStackType;
    uint16_t ncpStackVersion;
    uint8_t ncpEzspProtocolVersion = ezspVersion(EZSP_PROTOCOL_VERSION, &ncpStackType, &ncpStackVersion);

    printf("ezsp ver 0x%X stack type 0x%X stack ver 0x%02X\n", ncpEzspProtocolVersion, ncpStackType, ncpStackVersion);

    uint16_t manufacturerId;
    uint16_t versionNumber;

    status = ezspGetXncpInfo(&manufacturerId, &versionNumber);

    if (status == EMBER_SUCCESS) {
        printf("xncp mfg 0x%02X ver 0x%02X\n", manufacturerId, versionNumber);
    } else {
        printf("no xncp\n");
    }

    return 0;
}
