#include "node.h"

#include "endpoint.h"
#include "network.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

using namespace zigbee;

Node::Node(std::weak_ptr<Network> network, std::weak_ptr<INetworkListener> listener, EUI64 eui64, NodeId nodeId, uint8_t capability)
    : network_(network)
    , listener_(listener)
    , eui64_(eui64)
    , capability_(CapabilityFlags::FromBaseType(capability))
    , nodeId_(nodeId)
{
}

void Node::handleMessage(uint8_t endpointId, uint16_t profileId, uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload) {
    std::shared_ptr<Endpoint> endpoint;
    bool newEndpoint = false;

    {
        std::scoped_lock lock(mutex_);

        auto it = endpoints_.find(endpointId);
        if (it == endpoints_.end()) {
            YIO_LOG_INFO("Creating endpoint ad hoc:"
                         << " endpoint=" << static_cast<uint32_t>(endpointId)
                         << " profileId=" << quasar::Hex(profileId));

            SimpleDescriptor descriptor = {
                .endpoint = endpointId,
                .profileId = profileId};

            auto endpoint = std::make_shared<Endpoint>(weak_from_this(), std::move(descriptor));

            it = endpoints_.emplace(endpointId, std::move(endpoint)).first;

            newEndpoint = true;
        }

        endpoint = it->second;
    }

    if (newEndpoint) {
        if (auto listener = listener_.lock()) {
            listener->onNodeEndpointDiscovered(shared_from_this(), endpoint);
        }
    }

    endpoint->handleMessage(clusterId, header, std::move(payload));
}

void Node::sendMessage(uint8_t endpoint, uint16_t profileId, uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload) {
    if (auto network = network_.lock()) {
        network->sendMessage(nodeId_, endpoint, profileId, clusterId, header, std::move(payload));
    }
}

void Node::bind(uint8_t endpoint, uint16_t clusterId) {
    if (auto network = network_.lock()) {
        network->bind(getNodeId(), eui64_, endpoint, clusterId);
    }
}

std::shared_ptr<Network> Node::getNetwork() {
    return network_.lock();
}

const EUI64& Node::getEui64() const {
    return eui64_;
}

const Node::CapabilityFlags& Node::getCapability() const {
    return capability_;
}

NodeId Node::getNodeId() const {
    std::scoped_lock lock(mutex_);

    return nodeId_;
}

void Node::setNodeId(NodeId nodeId) {
    std::scoped_lock lock(mutex_);

    nodeId_ = nodeId;
}

std::shared_ptr<Endpoint> Node::addEndpoint(SimpleDescriptor descriptor) {
    std::scoped_lock lock(mutex_);

    const auto id = descriptor.endpoint;

    auto it = endpoints_.find(id);
    if (it != endpoints_.end()) {
        it->second->setDescriptor(std::move(descriptor));
        return it->second;
    }

    auto endpoint = std::make_shared<Endpoint>(weak_from_this(), SimpleDescriptor());
    endpoint->setDescriptor(std::move(descriptor));
    endpoints_.emplace(id, endpoint);
    return endpoint;
}

std::map<uint8_t, std::shared_ptr<Endpoint>> Node::getEndpoints() {
    std::scoped_lock lock(mutex_);

    return endpoints_;
}

std::shared_ptr<Endpoint> Node::getEndpoint(uint8_t endpointId) {
    std::scoped_lock lock(mutex_);

    auto it = endpoints_.find(endpointId);
    if (it != endpoints_.end()) {
        return it->second;
    }

    return nullptr;
}

std::shared_ptr<Endpoint> Node::getOrCreateEndpoint(uint8_t endpointId, uint16_t profileId) {
    std::scoped_lock lock(mutex_);

    auto it = endpoints_.find(endpointId);
    if (it != endpoints_.end()) {
        return it->second;
    }

    YIO_LOG_INFO(
        "Creating endpoint:"
        << " endpoint=" << static_cast<uint32_t>(endpointId)
        << " profileId=" << quasar::Hex(profileId));

    SimpleDescriptor descriptor = {
        .endpoint = endpointId,
        .profileId = profileId};

    auto endpoint = std::make_shared<Endpoint>(weak_from_this(), std::move(descriptor));

    endpoints_.emplace(endpointId, endpoint);

    return endpoint;
}

ZigbeeNode Node::dump() const {
    std::scoped_lock lock(mutex_);

    ZigbeeNode info = {
        .nodeId = nodeId_,
        .eui64 = eui64_,
        .capability = capability_};

    for (const auto& [_, endpoint] : endpoints_) {
        info.endpoints.push_back(endpoint->dump());
    }

    return info;
}
