#include "network.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("zigbee");

using namespace zigbee;

INetworkListener::~INetworkListener() = default;

IClusterFactory::~IClusterFactory() = default;

Network::Network(std::unique_ptr<INcp> ncp, std::weak_ptr<INetworkListener> listener, std::shared_ptr<IClusterFactory> clusterFactory)
    : ncp_(std::move(ncp))
    , listener_(listener)
    , clusterFactory_(clusterFactory)
{
}

void Network::init(NetworkParameters parameters) {
    {
        std::scoped_lock lock(mutex_);

        parameters_ = parameters;

        nodes_.clear();
        nodeIds_.clear();

        endpointsToDiscover_.clear();
    }

    for (const auto& [_, info] : parameters.nodes) {
        addNode(info);
    }

    ncp_->setListener(weak_from_this());
    ncp_->init();
}

void Network::stop() {
    {
        std::unique_lock lock(mutex_);

        parameters_.reset();
    }

    ncp_->init();
}

NetworkParameters Network::dump() const {
    std::scoped_lock lock(mutex_);

    if (!parameters_) {
        return NetworkParameters{};
    }

    NetworkParameters parameters = *parameters_;
    parameters.nodes.clear();

    for (const auto& [eui64, node] : nodes_) {
        parameters.nodes.emplace(eui64, node->dump());
    }

    return parameters;
}

const EUI64& Network::getEui64() {
    return ncp_->getEui64();
}

void Network::open(std::chrono::seconds duration) {
    ncp_->openNetwork(duration.count());
}

void Network::close() {
    ncp_->closeNetwork();
}

std::shared_ptr<Node> Network::addNode(ZigbeeNode info) {
    std::scoped_lock lock(mutex_);

    auto node = std::make_shared<Node>(weak_from_this(), listener_, info.eui64, info.nodeId, info.capability);

    for (const auto& descriptor : info.endpoints) {
        node->addEndpoint(descriptor);
    }

    nodes_.emplace(info.eui64, node);
    nodeIds_.emplace(info.nodeId, info.eui64);

    return node;
}

std::map<EUI64, std::shared_ptr<Node>> Network::getNodes() {
    std::scoped_lock lock(mutex_);

    return nodes_;
}

std::shared_ptr<Node> Network::getNode(const EUI64& eui64) {
    std::scoped_lock lock(mutex_);

    auto it = nodes_.find(eui64);
    if (it == nodes_.end()) {
        return nullptr;
    }

    return it->second;
}

bool Network::removeNode(const EUI64& eui64) {
    std::scoped_lock lock(mutex_);

    auto it = nodes_.find(eui64);
    if (it == nodes_.end()) {
        return false;
    }

    const auto& node = it->second;

    try {
        ncp_->requestLeave(node->getNodeId(), eui64);
    } catch (const std::exception& e) {
        YIO_LOG_WARN("Failed to remove node: " << e.what());
        return false;
    }

    // XXX erase only when node has actually left
    nodeIds_.erase(node->getNodeId());
    nodes_.erase(it);

    return true;
}

void Network::sendMessage(NodeId nodeId, uint8_t endpoint, uint16_t profileId, uint16_t clusterId, ZclHeader header, std::vector<uint8_t> payload) {
    try {
        ncp_->sendZclCommand(
            nodeId,
            profileId,
            clusterId,
            1,
            endpoint,
            header.getFrameType(),
            header.isManufacturerSpecific() ? std::make_optional(header.manufacturerCode) : std::nullopt,
            header.getDirection(),
            header.commandId,
            payload);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("zigbee.Network.ZclSendFailed", "Failed to send ZCL command: " << e.what());
    }
}

void Network::bind(NodeId nodeId, const EUI64& eui64, uint8_t endpoint, uint16_t clusterId) {
    try {
        ncp_->sendBindRequest(nodeId, eui64, endpoint, clusterId, 1);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("zigbee.Network.BindFailed", "Failed to send bind request: " << e.what());
    }
}

std::shared_ptr<zcl::ICluster> Network::createCluster(std::weak_ptr<Endpoint> endpoint, uint16_t clusterId) const {
    return clusterFactory_->createCluster(endpoint, clusterId);
}

Json::Value Network::collectNcpMetrics() {
    return ncp_->collectMetrics();
}

void Network::onStateChanged(NcpState state) {
    YIO_LOG_INFO("onStateChanged " << static_cast<int>(state));

    switch (state) {
        case NcpState::Uninitialized:
            YIO_LOG_WARN("NCP uninitialized");
            break;
        case NcpState::Initialized: {
            YIO_LOG_INFO("NCP initialized");

            std::scoped_lock lock(mutex_);

            if (parameters_) {
                YIO_LOG_INFO("Forming network");
                ncp_->formNetwork(*parameters_);
            }

            break;
        }
        case NcpState::Failed:
            YIO_LOG_INFO("NCP failed");
            break;
    }
}

void Network::onNetworkOpen(bool open)
{
    YIO_LOG_INFO("onNetworkOpen: " << open);

    if (auto listener = listener_.lock()) {
        if (open) {
            listener->onNetworkOpened();
        } else {
            listener->onNetworkClosed();
        }
    }
}

bool Network::onRawMessage(zigbee::NodeId nodeId, uint16_t profileId, uint16_t clusterId, const std::vector<uint8_t>& message) {
    Y_UNUSED(profileId);
    Y_UNUSED(clusterId);
    Y_UNUSED(message);

    if (auto node = getNode(nodeId)) {
        if (auto listener = listener_.lock()) {
            listener->onNodeRx(std::move(node));
        }
    }

    return false;
}

void Network::onDeviceAnnounce(NodeId nodeId, EUI64 eui64, uint8_t capability)
{
    YIO_LOG_INFO("onDeviceAnnounce: "
                 << " nodeId " << quasar::Hex(nodeId)
                 << " eui64 " << quasar::Hex(eui64)
                 << " capability " << quasar::Hex(capability));

    std::shared_ptr<Node> node;

    {
        std::scoped_lock lock(mutex_);

        auto it = nodes_.find(eui64);
        if (it != nodes_.end()) {
            node = it->second;

            NodeId currentNodeId = node->getNodeId();

            if (currentNodeId != nodeId) {
                YIO_LOG_INFO("Device "
                             << quasar::Hex(eui64)
                             << " nodeId changed from " << quasar::Hex(currentNodeId)
                             << " to " << quasar::Hex(nodeId));

                nodeIds_.erase(currentNodeId);
                nodeIds_[nodeId] = eui64;
                node->setNodeId(nodeId);
            }
        } else {
            node = std::make_shared<Node>(weak_from_this(), listener_, eui64, nodeId, capability);
            nodes_.emplace(eui64, node);
            nodeIds_.emplace(nodeId, eui64);
        }
    }

    if (auto listener = listener_.lock()) {
        listener->onNodeJoined(node);
    }

    try {
        ncp_->requestActiveEndpoints(nodeId);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("zigbee.Network.FailedToRequestActiveEndpoints", "Failed to requestActiveEndpoints: " << e.what());
    }
}

void Network::onActiveEndpoints(NodeId nodeId, const std::vector<uint8_t>& endpoints)
{
    YIO_LOG_INFO("onActiveEndpoints:"
                 << " nodeId=" << quasar::Hex(nodeId)
                 << " count=" << endpoints.size());

    try {
        {
            std::scoped_lock lock(mutex_);

            endpointsToDiscover_[nodeId] = endpoints.size();
        }

        for (auto endpoint : endpoints) {
            ncp_->requestSimpleDescriptor(nodeId, endpoint);
        }
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("zigbee.Network.FailedToRequestSimpleDescriptor", "Failed to requestSimpleDescriptor: " << e.what());
    }
}

void Network::onSimpleDescriptor(zigbee::NodeId nodeId, const zigbee::SimpleDescriptor& descriptor)
{
    std::ostringstream input;
    for (auto cluster : descriptor.inputClusters) {
        input << " " << quasar::Hex(cluster);
    }
    std::ostringstream output;
    for (auto cluster : descriptor.outputClusters) {
        output << " " << quasar::Hex(cluster);
    }
    YIO_LOG_INFO("onSimpleDescriptor: "
                 << " nodeId=" << quasar::Hex(nodeId)
                 << ", ep=" << static_cast<uint32_t>(descriptor.endpoint)
                 << ", profileId=" << quasar::Hex(descriptor.profileId)
                 << ", deviceId=" << quasar::Hex(descriptor.deviceId)
                 << ", deviceVersion=" << static_cast<uint32_t>(descriptor.deviceVersion)
                 << ", inputClusters=" << input.str()
                 << ", outputClusters=" << output.str());

    auto node = getNode(nodeId);
    if (!node) {
        YIO_LOG_ERROR_EVENT("zigbee.Network.UnknownNodeId", "Unknown nodeId " << quasar::Hex(nodeId));
        return;
    }

    auto endpoint = node->addEndpoint(descriptor);

    if (auto listener = listener_.lock()) {
        listener->onNodeEndpointDiscovered(node, endpoint);
    }

    bool discoveryComplete = false;

    {
        std::scoped_lock lock(mutex_);

        auto it = endpointsToDiscover_.find(nodeId);
        if (it != endpointsToDiscover_.end() && it->second > 0) {
            if (--it->second == 0) {
                discoveryComplete = true;
            }
        }
    }

    if (discoveryComplete) {
        if (auto listener = listener_.lock()) {
            listener->onNodeDiscoveryComplete(node);
        }
    }
}

void Network::onZclMessage(
    zigbee::NodeId nodeId,
    uint8_t endpoint,
    uint16_t profileId,
    uint16_t clusterId,
    zigbee::ZclHeader header,
    std::vector<uint8_t> payload)
{
    YIO_LOG_INFO("onZclMessage:"
                 << " nodeId=" << quasar::Hex(nodeId)
                 << " endpoint=" << static_cast<uint32_t>(endpoint)
                 << ", profileId=" << quasar::Hex(profileId)
                 << ", clusterId=" << quasar::Hex(clusterId)
                 << ", header=" << header.toString()
                 << ", payload=" << quasar::Hex(payload)
                 << ", payloadSize=" << payload.size());

    auto node = getNode(nodeId);
    if (!node) {
        YIO_LOG_WARN("onZclMessage for an unknown nodeId=" << quasar::Hex(nodeId));
        return;
    }

    try {
        node->handleMessage(endpoint, profileId, clusterId, header, std::move(payload));
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("zigbee.Network.FailedToHandleZclMessage", "Command handling failed: " << e.what());
    }
}

std::shared_ptr<Node> Network::getNode(NodeId nodeId) {
    std::scoped_lock lock(mutex_);

    const auto it = nodeIds_.find(nodeId);
    if (it == nodeIds_.end()) {
        return nullptr;
    }

    const auto nodeIt = nodes_.find(it->second);
    if (nodeIt == nodes_.end()) {
        return nullptr;
    }

    return nodeIt->second;
}
