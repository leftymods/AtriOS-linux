#pragma once

#include "hci_command.h"
#include "hci_command_handler.h"

namespace telink {

    struct ZclRspHdr {
        static constexpr size_t SIZE = 5;

        uint16_t srcAddr;
        uint8_t srcEp;
        uint8_t dstEp;
        uint8_t seqNum;

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    struct ZigbeeBdbCommissionFormation: public HciCommand<0x0001> {
    };

    struct ZigbeeBdbFactoryReset: public HciCommand<0x0005> {
    };

    struct ZigbeeBdbChannelSet: public HciCommand<0x0007> {
        uint8_t channel;

    public:
        std::vector<uint8_t> serialize() const;
    };

    struct ZigbeeDiscoverySimpleDescReq: public HciCommand<0x0013> {
        uint16_t dstAddr;
        uint16_t nwkAddrOfInterest;
        uint8_t endpoint;

    public:
        std::vector<uint8_t> serialize() const;
    };

    struct ZigbeeDiscoverySimpleDescRsp: public HciCommand<0x8013> {
        uint16_t srcAddr;
        uint8_t seqNum;
        uint8_t status;
        uint16_t nwkAddrOfInterest;
        uint8_t length;
        std::vector<uint8_t> simpleDesc;

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    struct ZigbeeDiscoveryActiveEpReq: public HciCommand<0x0015> {
        uint16_t dstAddr;
        uint16_t nwkAddrOfInterest;

    public:
        std::vector<uint8_t> serialize() const;
    };

    struct ZigbeeDiscoveryActiveEpRsp: public HciCommand<0x8015> {
        uint16_t srcAddr;
        uint8_t seqNum;
        uint8_t status;
        uint16_t nwkAddrOfInterest;
        uint8_t activeEpCount;
        std::vector<uint8_t> epList;

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    struct ZigbeeMgmtPermitJoinReq: public HciCommand<0x0034> {
        uint16_t dstAddr;
        uint8_t permitDuration;
        uint8_t tcSignificance;

    public:
        std::vector<uint8_t> serialize() const;
    };

    struct ZigbeeNodesDevAnnceInd: public HciCommand<0x8043> {
        uint16_t nwkAddr;
        uint8_t ieeeAddr[8];
        uint8_t capability;

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    struct ZigbeeGetLocalNwkInfoReq: public HciCommand<0x0045> {
    };

    struct ZigbeeGetLocalNwkInfoRsp: public HciCommand<0x8045> {
        uint8_t devType;
        uint8_t capability;
        uint8_t onANetwork;
        uint16_t panId;
        uint8_t extPanID[8];
        uint16_t nwkAddr;
        uint8_t ieeeAddr[8];

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    struct ZigbeeZclReportMsgRcv: public HciCommand<0x8104> {
        ZclRspHdr header;
        uint16_t clusterId;
        uint8_t attrNum;
        std::vector<uint8_t> attrList;

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    template <class T>
    using ZigbeeCommandHandler = HciCommandHandler<
        T,
        ZigbeeDiscoverySimpleDescRsp,
        ZigbeeDiscoveryActiveEpRsp,
        ZigbeeNodesDevAnnceInd,
        ZigbeeGetLocalNwkInfoRsp,
        ZigbeeZclReportMsgRcv>;

} // namespace telink
