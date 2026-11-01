#pragma once

#include <smart_devices/libs/zigbee/zcl/i_cluster.h>

namespace zigbee {

    class Node;

    class Endpoint: public std::enable_shared_from_this<Endpoint> {
    public:
        Endpoint(std::weak_ptr<Node> node, SimpleDescriptor descriptor);

    public:
        std::shared_ptr<Node> getNode();

        void handleMessage(uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload);

        void sendMessage(uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload);

        void bind(uint16_t clusterId);

        void setDescriptor(SimpleDescriptor descriptor);

        uint8_t getEndpointId() const;
        uint16_t getProfileId() const;
        uint16_t getDeviceId() const;

        std::map<uint16_t, std::shared_ptr<zcl::ICluster>> getClusters();
        std::shared_ptr<zcl::ICluster> getCluster(uint16_t clusterId);

        template <typename Cluster>
        std::shared_ptr<Cluster> getCluster() {
            return std::static_pointer_cast<Cluster>(getCluster(Cluster::Id));
        }

        SimpleDescriptor dump() const;

    private:
        std::shared_ptr<zcl::ICluster> createCluster(uint16_t clusterId);

    private:
        const std::weak_ptr<Node> node_;

        mutable std::mutex mutex_;

        SimpleDescriptor descriptor_;

        std::map<uint16_t, std::shared_ptr<zcl::ICluster>> clusters_;
    };

} // namespace zigbee
