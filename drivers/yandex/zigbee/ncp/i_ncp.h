#pragma once

#include "ncp_listener.h"

#include <smart_devices/libs/zigbee/zigbee.h>

#include <yandex_io/libs/json_utils/json_utils.h>

#include <string>
#include <optional>

namespace zigbee {
    struct FirmwareVersion {
        uint16_t xncpManufacturerId;
        uint16_t xncpVersion;

        auto operator<=>(const FirmwareVersion&) const = default;
    };

    class INcp {
    public:
        virtual ~INcp();

        virtual void launchBootloader() = 0;

        virtual FirmwareVersion getVersion() = 0;

        virtual const EUI64& getEui64() = 0;

        virtual void formNetwork(const NetworkParameters& parameters) = 0;

        virtual void leaveNetwork() = 0;

        virtual void openNetwork(uint8_t duration) = 0;

        virtual void closeNetwork() = 0;

        virtual void sendMessage(
            NodeId destination,
            uint16_t profileId,
            uint16_t clusterId,
            uint8_t sourceEndpoint,
            uint8_t destinationEndpoint,
            const std::vector<uint8_t>& message) = 0;

        virtual void sendZclCommand(
            NodeId destination,
            uint16_t profileId,
            uint16_t clusterId,
            uint8_t sourceEndpoint,
            uint8_t destinationEndpoint,
            ZclFrameType frameType,
            std::optional<uint16_t> manufacturerCode,
            ZclDirection direction,
            uint8_t commandId,
            const std::vector<uint8_t>& payload) = 0;

        virtual void requestActiveEndpoints(NodeId target) = 0;

        virtual void requestSimpleDescriptor(NodeId target, uint8_t endpoint) = 0;

        virtual void requestLeave(NodeId target, EUI64 deviceAddress) = 0;

        virtual void sendBindRequest(NodeId target, const EUI64& source, uint8_t sourceEndpoint, uint16_t clusterId, uint8_t destinationEndpoint) = 0;

        virtual void init() = 0;

        virtual void setListener(std::weak_ptr<INcpListener> listener) = 0;

        virtual Json::Value collectMetrics() = 0;
    };
} // namespace zigbee
