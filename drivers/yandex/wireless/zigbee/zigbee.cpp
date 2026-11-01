#include "zigbee.h"

#include <yandex_io/libs/base/utils.h>

#include <sstream>

using namespace zigbee;

namespace {

    uint8_t getBitValue(uint8_t byte, uint8_t bitIndex) {
        return (byte >> bitIndex) & 1U;
    }

    template <class C>
    bool isZero(const C& c) {
        return std::all_of(c.begin(), c.end(), [](auto v) {
            return v == 0;
        });
    }

} // namespace

bool NetworkParameters::isValid() const {
    if (channel < 11 || channel > 26) {
        return false;
    }

    if (isZero(extendedPanId)) {
        return false;
    }

    return true;
}

NetworkParameters NetworkParameters::fromProto(const proto::NetworkParameters& proto) {
    NetworkParameters parameters = {};

    parameters.panId = proto.pan_id();

    if (proto.extended_pan_id().size() != parameters.extendedPanId.size()) {
        throw std::runtime_error("Invalid extended PAN ID size");
    }
    std::copy(proto.extended_pan_id().begin(), proto.extended_pan_id().end(), parameters.extendedPanId.begin());

    if (proto.network_key().size() != parameters.networkKey.size()) {
        throw std::runtime_error("Invalid network key size");
    }
    std::copy(proto.network_key().begin(), proto.network_key().end(), parameters.networkKey.begin());

    parameters.networkKeySequenceNumber = proto.network_key_sequence_number();

    if (proto.trust_center_link_key().size() != parameters.trustCenterLinkKey.size()) {
        throw std::runtime_error("Invalid link key size");
    }
    std::copy(proto.trust_center_link_key().begin(), proto.trust_center_link_key().end(), parameters.trustCenterLinkKey.begin());

    parameters.channel = proto.channel();
    for (const auto& nodeProto : proto.nodes()) {
        auto node = ZigbeeNode::fromProto(nodeProto);
        parameters.nodes.emplace(node.eui64, node);
    }

    return parameters;
}

proto::NetworkParameters NetworkParameters::toProto() const {
    proto::NetworkParameters proto;
    proto.set_version(proto::NetworkParameters_Version_FIRST);

    proto.set_pan_id(panId);

    {
        auto& id = *proto.mutable_extended_pan_id();
        id.resize(extendedPanId.size());
        std::copy(extendedPanId.begin(), extendedPanId.end(), id.begin());
    }

    {
        auto& key = *proto.mutable_network_key();
        key.resize(networkKey.size());
        std::copy(networkKey.begin(), networkKey.end(), key.begin());
    }

    proto.set_network_key_sequence_number(networkKeySequenceNumber);

    {
        auto& key = *proto.mutable_trust_center_link_key();
        key.resize(trustCenterLinkKey.size());
        std::copy(trustCenterLinkKey.begin(), trustCenterLinkKey.end(), key.begin());
    }

    proto.set_channel(channel);

    for (const auto& node : nodes) {
        proto.mutable_nodes()->Add(node.second.toProto());
    }

    return proto;
}

SimpleDescriptor SimpleDescriptor::fromProto(const proto::SimpleDescriptor& proto) {
    if (!std::in_range<decltype(SimpleDescriptor::endpoint)>(proto.endpoint())) {
        throw std::runtime_error("Invalid endpoint size");
    }

    if (!std::in_range<decltype(SimpleDescriptor::profileId)>(proto.profileid())) {
        throw std::runtime_error("Invalid profileid size");
    }

    if (!std::in_range<decltype(SimpleDescriptor::deviceId)>(proto.deviceid())) {
        throw std::runtime_error("Invalid deviceId size");
    }

    if (!std::in_range<decltype(SimpleDescriptor::deviceVersion)>(proto.deviceversion())) {
        throw std::runtime_error("Invalid deviceVersion size");
    }

    if (std::any_of(proto.inputclusters().begin(), proto.inputclusters().begin(), [](auto v) { return !std::in_range<decltype(SimpleDescriptor::inputClusters)::value_type>(v); })) {
        throw std::runtime_error("Invalid inputCluster value");
    }

    if (std::any_of(proto.outputclusters().begin(), proto.outputclusters().begin(), [](auto v) { return !std::in_range<decltype(SimpleDescriptor::outputClusters)::value_type>(v); })) {
        throw std::runtime_error("Invalid outputCluster value");
    }

    decltype(SimpleDescriptor::inputClusters) inputClusters(proto.inputclusters().size());
    std::copy(proto.inputclusters().begin(), proto.inputclusters().end(), inputClusters.begin());

    decltype(SimpleDescriptor::outputClusters) outputClusters(proto.outputclusters().size());
    std::copy(proto.outputclusters().begin(), proto.outputclusters().end(), outputClusters.begin());

    return SimpleDescriptor{
        .endpoint = static_cast<decltype(SimpleDescriptor::endpoint)>(proto.endpoint()),
        .profileId = static_cast<decltype(SimpleDescriptor::profileId)>(proto.profileid()),
        .deviceId = static_cast<decltype(SimpleDescriptor::deviceId)>(proto.deviceid()),
        .deviceVersion = static_cast<decltype(SimpleDescriptor::deviceVersion)>(proto.deviceversion()),
        .inputClusters = std::move(inputClusters),
        .outputClusters = std::move(outputClusters),
    };
}

proto::SimpleDescriptor SimpleDescriptor::toProto() const {
    proto::SimpleDescriptor sd;
    sd.set_endpoint(endpoint);
    sd.set_profileid(profileId);
    sd.set_deviceid(deviceId);
    sd.set_deviceversion(deviceVersion);
    *sd.mutable_inputclusters() = {inputClusters.begin(), inputClusters.end()};
    *sd.mutable_outputclusters() = {outputClusters.begin(), outputClusters.end()};
    return sd;
}

ZigbeeNode ZigbeeNode::fromProto(const proto::ZigbeeNode& proto) {
    if (!std::in_range<decltype(ZigbeeNode::nodeId)>(proto.nodeid())) {
        throw std::runtime_error("Invalid nodeId size");
    }

    if (!std::in_range<decltype(ZigbeeNode::capability)>(proto.capability())) {
        throw std::runtime_error("Invalid capability size");
    }

    decltype(ZigbeeNode::endpoints) endpoints(proto.endpoints().size());
    std::transform(proto.endpoints().begin(), proto.endpoints().end(), endpoints.begin(), [](const auto& e) { return SimpleDescriptor::fromProto(e); });

    return ZigbeeNode{
        .nodeId = static_cast<decltype(ZigbeeNode::nodeId)>(proto.nodeid()),
        .eui64 = {static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() >> 56 & 0xFF),
                  static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() >> 48 & 0xFF),
                  static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() >> 40 & 0xFF),
                  static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() >> 32 & 0xFF),
                  static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() >> 24 & 0xFF),
                  static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() >> 16 & 0xFF),
                  static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() >> 8 & 0xFF),
                  static_cast<decltype(zigbee::ZigbeeNode::eui64)::value_type>(proto.eui64() & 0xFF)},
        .capability = static_cast<decltype(ZigbeeNode::capability)>(proto.capability()),
        .endpoints = std::move(endpoints),
    };
}

proto::ZigbeeNode ZigbeeNode::toProto() const {
    proto::ZigbeeNode zn;
    zn.set_nodeid(nodeId);

    zn.set_eui64(static_cast<decltype(zn.eui64())>(eui64[0]) << 56 |
                 static_cast<decltype(zn.eui64())>(eui64[1]) << 48 |
                 static_cast<decltype(zn.eui64())>(eui64[2]) << 40 |
                 static_cast<decltype(zn.eui64())>(eui64[3]) << 32 |
                 static_cast<decltype(zn.eui64())>(eui64[4]) << 24 |
                 static_cast<decltype(zn.eui64())>(eui64[5]) << 16 |
                 static_cast<decltype(zn.eui64())>(eui64[6]) << 8 |
                 static_cast<decltype(zn.eui64())>(eui64[7]));
    zn.set_capability(capability);

    for (const auto& endpoint : endpoints) {
        zn.mutable_endpoints()->Add(endpoint.toProto());
    }
    return zn;
}

std::string ZclHeader::toString() const {
    std::stringstream ss;
    ss << "(frameControl=" << quasar::Hex(frameControl)
       << "[frameType=" << (getFrameType() == ZclFrameType::GLOBAL ? "global" : "cluster_specific")
       << ", direction=" << (getDirection() == ZclDirection::CLIENT_TO_SERVER ? "client_to_server" : "server_to_client")
       << "], manufacturerCode=" << quasar::Hex(manufacturerCode)
       << ", transactionSequenceNumber=" << quasar::Hex(transactionSequenceNumber)
       << ", commandId=" << quasar::Hex(commandId)
       << ")";

    return ss.str();
}

ZclFrameType ZclHeader::getFrameType() const {
    return (frameControl & 3) == 0b00 ? ZclFrameType::GLOBAL : ZclFrameType::CLUSTER_SPECIFIC;
}

ZclDirection ZclHeader::getDirection() const {
    return getBitValue(frameControl, 3) == 0 ? ZclDirection::CLIENT_TO_SERVER : ZclDirection::SERVER_TO_CLIENT;
}

bool ZclHeader::isManufacturerSpecific() const {
    return getBitValue(frameControl, 2) == 1;
}

bool ZclHeader::isDisableDefaultResponse() const {
    return getBitValue(frameControl, 4) == 1;
}
