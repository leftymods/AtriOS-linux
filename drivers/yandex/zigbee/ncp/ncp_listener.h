#pragma once

#include <smart_devices/libs/zigbee/zigbee.h>

namespace zigbee {

    enum class NcpState {
        Uninitialized,
        Initialized,
        Failed,
    };

    class INcpListener {
    public:
        virtual ~INcpListener() = default;

        virtual void onStateChanged(NcpState state) = 0;

        virtual void onNetworkOpen(bool open) = 0;

        virtual bool onRawMessage(zigbee::NodeId nodeId, uint16_t profileId, uint16_t clusterId, const std::vector<uint8_t>& message) = 0;

        virtual void onDeviceAnnounce(NodeId nodeId, EUI64 eui64, uint8_t capability) = 0;

        virtual void onActiveEndpoints(NodeId nodeId, const std::vector<uint8_t>& endpoints) = 0;

        virtual void onSimpleDescriptor(NodeId nodeId, const SimpleDescriptor& endpoint) = 0;

        virtual void onZclMessage(NodeId nodeId, uint8_t endpoint, uint16_t profileId, uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload) = 0;
    };

} // namespace zigbee
