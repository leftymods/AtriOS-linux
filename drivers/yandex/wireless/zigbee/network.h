#pragma once

#include "node.h"

#include <smart_devices/libs/zigbee/ncp/i_ncp.h>
#include <smart_devices/libs/zigbee/zcl/i_cluster.h>

#include <yandex_io/libs/json_utils/json_utils.h>

#include <shared_mutex>

namespace zigbee {

    class INetworkListener {
    public:
        virtual ~INetworkListener();

    public:
        virtual void onNetworkOpened() = 0;
        virtual void onNetworkClosed() = 0;

        virtual void onNodeJoined(std::shared_ptr<Node> node) = 0;
        virtual void onNodeEndpointDiscovered(std::shared_ptr<Node> node, std::shared_ptr<Endpoint> endpoint) = 0;
        virtual void onNodeDiscoveryComplete(std::shared_ptr<Node> node) = 0;
        virtual void onNodeRx(std::shared_ptr<Node> node) = 0;
    };

    class IClusterFactory {
    public:
        virtual ~IClusterFactory();

        virtual std::shared_ptr<zcl::ICluster> createCluster(std::weak_ptr<Endpoint> endpoint, uint16_t clusterId) const = 0;
    };

    class Network
        : public INcpListener,
          public std::enable_shared_from_this<Network> {
    public:
        Network(std::unique_ptr<INcp> ncp, std::weak_ptr<INetworkListener> listener, std::shared_ptr<IClusterFactory> clusterFactory);

    public:
        void init(NetworkParameters parameters);
        void stop();
        NetworkParameters dump() const;

        const EUI64& getEui64();

        void open(std::chrono::seconds duration);
        void close();

        std::shared_ptr<Node> addNode(ZigbeeNode info);
        std::map<EUI64, std::shared_ptr<Node>> getNodes();
        std::shared_ptr<Node> getNode(const EUI64& eui64);
        bool removeNode(const EUI64& eui64);

        void sendMessage(NodeId nodeId, uint8_t endpoint, uint16_t profileId, uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload);

        void bind(NodeId nodeId, const EUI64& eui64, uint8_t endpoint, uint16_t clusterId);

        std::shared_ptr<zcl::ICluster> createCluster(std::weak_ptr<Endpoint> endpoint, uint16_t clusterId) const;

        Json::Value collectNcpMetrics();

    private:
        void onStateChanged(NcpState state) override;

        void onNetworkOpen(bool open) override;

        bool onRawMessage(NodeId nodeId, uint16_t profileId, uint16_t clusterId, const std::vector<uint8_t>& message) override;

        void onDeviceAnnounce(NodeId nodeId, EUI64 eui64, uint8_t capability) override;

        void onActiveEndpoints(NodeId nodeId, const std::vector<uint8_t>& endpoints) override;

        void onSimpleDescriptor(NodeId nodeId, const SimpleDescriptor& descriptor) override;

        void onZclMessage(
            NodeId nodeId,
            uint8_t endpoint,
            uint16_t profileId,
            uint16_t clusterId,
            ZclHeader header,
            std::vector<uint8_t> payload) override;

    private:
        std::shared_ptr<Node> getNode(NodeId nodeId);

    private:
        const std::unique_ptr<INcp> ncp_;
        const std::weak_ptr<INetworkListener> listener_;
        const std::shared_ptr<IClusterFactory> clusterFactory_;

        mutable std::mutex mutex_;

        std::optional<NetworkParameters> parameters_;

        std::map<EUI64, std::shared_ptr<Node>> nodes_;
        std::map<NodeId, EUI64> nodeIds_;

        std::map<NodeId, size_t> endpointsToDiscover_;
    };

} // namespace zigbee
