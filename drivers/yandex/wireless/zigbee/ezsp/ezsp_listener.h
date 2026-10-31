#pragma once

#include <vector>

extern "C" {

#include PLATFORM_HEADER
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/stack/include/ember-types.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp/ezsp-protocol.h>
}

namespace zigbee {

    class IEzspListener {
    public:
        virtual ~IEzspListener() = default;

        virtual void onCounterRollover(EmberCounterType type) = 0;

        virtual void onStackStatus(EzspStatus status) = 0;
        virtual void onError(EzspStatus status) = 0;

        virtual void onMessageSent(
            EmberOutgoingMessageType type,
            uint16_t indexOrDestination,
            const EmberApsFrame& apsFrame,
            uint8_t messageTag,
            EmberStatus status,
            const std::vector<uint8_t>& message) = 0;

        virtual void onIncomingMessage(
            EmberIncomingMessageType type,
            const EmberApsFrame& apsFrame,
            uint8_t lastHopLqi,
            int8_t lastHopRssi,
            EmberNodeId sender,
            uint8_t bindingIndex,
            uint8_t addressIndex,
            const std::vector<uint8_t>& message) = 0;

        virtual void onMfglibRx(
            uint8_t lqi,
            int8_t rssi,
            const std::vector<uint8_t>& payload) = 0;
    };

} // namespace zigbee
