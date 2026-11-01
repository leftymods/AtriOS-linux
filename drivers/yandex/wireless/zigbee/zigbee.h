#pragma once

#include <smart_devices/libs/zigbee/proto/network.pb.h>

#include <array>

namespace zigbee {

    using NodeId = uint16_t;
    using EUI64 = std::array<uint8_t, 8>;

    using PanId = uint16_t;
    using ExtendedPanId = std::array<uint8_t, 8>;

    using EncryptionKey = std::array<uint8_t, 16>;

    struct SimpleDescriptor {
        uint8_t endpoint;
        uint16_t profileId;
        uint16_t deviceId;
        uint8_t deviceVersion;
        std::vector<uint16_t> inputClusters;
        std::vector<uint16_t> outputClusters;
        static SimpleDescriptor fromProto(const proto::SimpleDescriptor& proto);
        proto::SimpleDescriptor toProto() const;
    };

    struct ZigbeeNode {
        zigbee::NodeId nodeId;
        zigbee::EUI64 eui64;
        uint8_t capability;
        std::vector<zigbee::SimpleDescriptor> endpoints;
        static ZigbeeNode fromProto(const proto::ZigbeeNode& proto);
        proto::ZigbeeNode toProto() const;
    };

    struct NetworkParameters {
        PanId panId;
        ExtendedPanId extendedPanId;
        EncryptionKey networkKey;
        uint8_t networkKeySequenceNumber;
        EncryptionKey trustCenterLinkKey;
        uint8_t channel;
        std::map<zigbee::EUI64, zigbee::ZigbeeNode> nodes;

    public:
        bool isValid() const;

        static NetworkParameters fromProto(const proto::NetworkParameters& proto);
        proto::NetworkParameters toProto() const;
        friend inline bool operator==(const NetworkParameters& lhs, const NetworkParameters& rhs) {
            return lhs.panId == rhs.panId && lhs.extendedPanId == rhs.extendedPanId && lhs.networkKey == rhs.networkKey && lhs.networkKeySequenceNumber == rhs.networkKeySequenceNumber && lhs.trustCenterLinkKey == rhs.trustCenterLinkKey && lhs.channel == rhs.channel;
        }
    };

    enum class ZclFrameType {
        GLOBAL,
        CLUSTER_SPECIFIC
    };

    enum class ZclDirection {
        CLIENT_TO_SERVER,
        SERVER_TO_CLIENT
    };

    struct ZclHeader {
        uint8_t frameControl;
        uint16_t manufacturerCode;
        uint8_t transactionSequenceNumber;
        uint8_t commandId;

        std::string toString() const;
        ZclFrameType getFrameType() const;
        ZclDirection getDirection() const;
        bool isManufacturerSpecific() const;
        bool isDisableDefaultResponse() const;
    };

} // namespace zigbee
