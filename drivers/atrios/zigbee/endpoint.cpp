#include "endpoint.h"
#include "network.h"
#include "node.h"
#include "profiles.h"

#include <smart_devices/libs/zigbee/zcl/clusters.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

using namespace zigbee;

Endpoint::Endpoint(std::weak_ptr<Node> node, SimpleDescriptor descriptor)
    : node_(node)
    , descriptor_(std::move(descriptor))
{
}

std::shared_ptr<Node> Endpoint::getNode() {
    return node_.lock();
}

void Endpoint::handleMessage(uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload) {
    getCluster(clusterId)->handleCommand(header, payload);
}

void Endpoint::sendMessage(uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload) {
    auto profileId = getProfileId();
    if (profileId == ZLLProfile::Id) {
        profileId = ZHAProfile::Id;
    }

    if (auto node = node_.lock()) {
        node->sendMessage(descriptor_.endpoint, profileId, clusterId, header, std::move(payload));
    }
}

void Endpoint::bind(uint16_t clusterId) {
    if (auto node = node_.lock()) {
        node->bind(getEndpointId(), clusterId);
    }
}

void Endpoint::setDescriptor(SimpleDescriptor descriptor) {
    for (auto clusterId : descriptor.inputClusters) {
        getCluster(clusterId);
    }

    std::scoped_lock lock(mutex_);

    descriptor_ = std::move(descriptor);
}

uint8_t Endpoint::getEndpointId() const {
    std::scoped_lock lock(mutex_);

    return descriptor_.endpoint;
}

uint16_t Endpoint::getProfileId() const {
    std::scoped_lock lock(mutex_);

    return descriptor_.profileId;
}

uint16_t Endpoint::getDeviceId() const {
    std::scoped_lock lock(mutex_);

    return descriptor_.deviceId;
}

std::map<uint16_t, std::shared_ptr<zcl::ICluster>> Endpoint::getClusters() {
    std::scoped_lock lock(mutex_);

    return clusters_;
}

std::shared_ptr<zcl::ICluster> Endpoint::getCluster(uint16_t clusterId) {
    std::scoped_lock lock(mutex_);

    auto it = clusters_.find(clusterId);
    if (it == clusters_.end()) {
        auto cluster = createCluster(clusterId);

        clusters_.emplace(clusterId, cluster);

        return cluster;
    }

    return it->second;
}

SimpleDescriptor Endpoint::dump() const {
    std::scoped_lock lock(mutex_);

    return descriptor_;
}

std::shared_ptr<zcl::ICluster> Endpoint::createCluster(uint16_t clusterId) {
    if (auto node = node_.lock()) {
        if (auto network = node->getNetwork()) {
            return network->createCluster(weak_from_this(), clusterId);
        }
    }

    return nullptr;
}
