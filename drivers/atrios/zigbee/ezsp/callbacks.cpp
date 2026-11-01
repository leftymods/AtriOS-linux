#include "ezsp.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

extern "C" {

#include PLATFORM_HEADER
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/stack/include/error.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/stack/include/ember-types.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp/ezsp-enum.h>

    YIO_DEFINE_LOG_MODULE("zigbee");

    void ezspErrorHandler(EzspStatus status) {
        YIO_LOG_ERROR_EVENT("Ezsp.LogErrorStatus", "EZSP error: " << quasar::Hex(status));
        if (auto listener = zigbee::Ezsp::getInstance().getListener().lock()) {
            listener->onError(status);
        }
    }

    void ezspCounterRolloverHandler(EmberCounterType type) {
        YIO_LOG_DEBUG("ezspCounterRolloverHandler: " << quasar::Hex(type));

        if (auto listener = zigbee::Ezsp::getInstance().getListener().lock()) {
            listener->onCounterRollover(type);
        }
    }

    void ezspStackStatusHandler(EmberStatus status) {
        YIO_LOG_DEBUG("ezspStackStatusHandler: " << quasar::Hex(status));

        if (auto listener = zigbee::Ezsp::getInstance().getListener().lock()) {
            listener->onStackStatus(status);
        }
    }

    void ezspNetworkFoundHandler(
        EmberZigbeeNetwork* networkFound,
        uint8_t lastHopLqi,
        int8_t lastHopRssi) {
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
        EmberNodeType childType)
    {
        YIO_LOG_DEBUG(
            "ezspChildJoinHandler:"
            << " index " << static_cast<uint32_t>(index)
            << " joining " << joining
            << " childId " << quasar::Hex(childId)
            << " childEui64 0x" << HexEncode(childEui64, EUI64_SIZE)
            << " childType " << static_cast<uint32_t>(childType));
    }

    void ezspMessageSentHandler(
        EmberOutgoingMessageType type,
        uint16_t indexOrDestination,
        EmberApsFrame* apsFrame,
        uint8_t messageTag,
        EmberStatus status,
        uint8_t messageLength,
        uint8_t* messageContents)
    {
        YIO_LOG_DEBUG(
            "ezspMessageSentHandler:"
            << " node " << quasar::Hex(indexOrDestination)
            << " profile " << quasar::Hex(apsFrame->profileId)
            << " cluster " << quasar::Hex(apsFrame->clusterId)
            << " srcEp " << static_cast<uint32_t>(apsFrame->sourceEndpoint)
            << " dstEp " << static_cast<uint32_t>(apsFrame->destinationEndpoint)
            << " opts " << quasar::Hex(apsFrame->options)
            << " tag " << static_cast<uint32_t>(messageTag)
            << " status " << quasar::Hex(status)
            << " data 0x" << HexEncode(messageContents, messageLength));

        if (auto listener = zigbee::Ezsp::getInstance().getListener().lock()) {
            listener->onMessageSent(
                type,
                indexOrDestination,
                *apsFrame,
                messageTag,
                status,
                std::vector<uint8_t>{messageContents, messageContents + messageLength});
        }
    }

    void ezspPollHandler(
        EmberNodeId childId)
    {
        YIO_LOG_DEBUG(
            "ezspPollHandler:"
            << " childId " << quasar::Hex(childId));
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
        uint8_t* messageContents)
    {
        YIO_LOG_DEBUG("ezspIncomingMessageHandler:"
                      << " node " << quasar::Hex(sender)
                      << " profile " << quasar::Hex(apsFrame->profileId)
                      << " cluster " << quasar::Hex(apsFrame->clusterId)
                      << " srcEp " << (int)apsFrame->sourceEndpoint
                      << " dstEp " << (int)apsFrame->destinationEndpoint
                      << " opts " << quasar::Hex(apsFrame->options)
                      << " data 0x" << HexEncode(messageContents, messageLength));

        if (auto listener = zigbee::Ezsp::getInstance().getListener().lock()) {
            listener->onIncomingMessage(
                type,
                *apsFrame,
                lastHopLqi,
                lastHopRssi,
                sender,
                bindingIndex,
                addressIndex,
                std::vector<uint8_t>{messageContents, messageContents + messageLength});
        }
    }

    void ezspTrustCenterJoinHandler(
        EmberNodeId newNodeId,
        EmberEUI64 newNodeEui64,
        EmberDeviceUpdate status,
        EmberJoinDecision policyDecision,
        EmberNodeId parentOfNewNodeId)
    {
        YIO_LOG_DEBUG(
            "ezspTrustCenterJoinHandler:"
            << " newNodeId " << quasar::Hex(newNodeId)
            << " newNodeEui64 0x" << HexEncode(newNodeEui64, EUI64_SIZE)
            << " status " << static_cast<uint32_t>(status)
            << " policyDecision " << static_cast<uint32_t>(policyDecision)
            << " parentOfNewNodeId " << quasar::Hex(parentOfNewNodeId));
    }

    void ezspMfglibRxHandler(
        uint8_t linkQuality,
        int8_t rssi,
        uint8_t packetLength,
        uint8_t* packetContents) {
        if (auto listener = zigbee::Ezsp::getInstance().getListener().lock()) {
            listener->onMfglibRx(
                linkQuality,
                rssi,
                std::vector<uint8_t>{packetContents, packetContents + packetLength - 2});
        }
    }
}
