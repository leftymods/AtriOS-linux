#pragma once

#include "zigbee.h"

#include <util/generic/flags.h>

namespace zigbee {

    class Network;
    class INetworkListener;
    class Endpoint;

    class Node: public std::enable_shared_from_this<Node> {
    public:
        enum class Capability: uint8_t {
            AlternatePanCoordinator = 1 << 0,
            DeviceType = 1 << 1,
            PowerSource = 1 << 2,
            ReceiverOnWhenIdle = 1 << 3,
            Reserved4 = 1 << 4,
            Reserved5 = 1 << 5,
            SecurityCapability = 1 << 6,
            AllocateAddress = 1 << 7,
        };

        Y_DECLARE_FLAGS(CapabilityFlags, Capability);

    public:
        Node(std::weak_ptr<Network> network, std::weak_ptr<INetworkListener> listener, EUI64 eui64, NodeId nodeId, uint8_t capability);

    public:
        std::shared_ptr<Network> getNetwork();

        const EUI64& getEui64() const;
        const CapabilityFlags& getCapability() const;

        NodeId getNodeId() const;
        void setNodeId(NodeId nodeId);

        std::shared_ptr<Endpoint> addEndpoint(SimpleDescriptor descriptor);
        std::map<uint8_t, std::shared_ptr<Endpoint>> getEndpoints();
        std::shared_ptr<Endpoint> getEndpoint(uint8_t endpointId);
        std::shared_ptr<Endpoint> getOrCreateEndpoint(uint8_t endpointId, uint16_t profileId);

        ZigbeeNode dump() const;

        void handleMessage(uint8_t endpoint, uint16_t profileId, uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload);

        void sendMessage(uint8_t endpoint, uint16_t profileId, uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload);

        void bind(uint8_t endpoint, uint16_t clusterId);

    private:
        const std::weak_ptr<Network> network_;
        const std::weak_ptr<INetworkListener> listener_;

        const EUI64 eui64_;
        const CapabilityFlags capability_;

        mutable std::mutex mutex_;

        NodeId nodeId_;

        std::map<uint8_t, std::shared_ptr<Endpoint>> endpoints_;
    };

} // namespace zigbee
