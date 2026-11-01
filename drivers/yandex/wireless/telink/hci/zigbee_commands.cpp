#include "zigbee_commands.h"

using namespace telink;

bool ZclRspHdr::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() != 5) {
        return false;
    }

    srcAddr = (payload[0] << 8) | payload[1];
    srcEp = payload[2];
    dstEp = payload[3];
    seqNum = payload[4];

    return true;
}

std::vector<uint8_t> ZigbeeBdbChannelSet::serialize() const {
    return {
        channel,
    };
}

std::vector<uint8_t> ZigbeeDiscoverySimpleDescReq::serialize() const {
    return {
        static_cast<uint8_t>(dstAddr >> 8),
        static_cast<uint8_t>(dstAddr & 0xff),
        static_cast<uint8_t>(nwkAddrOfInterest >> 8),
        static_cast<uint8_t>(nwkAddrOfInterest & 0xff),
        endpoint,
    };
}

bool ZigbeeDiscoverySimpleDescRsp::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() < 7) {
        return false;
    }

    srcAddr = (payload[0] << 8) | payload[1];
    seqNum = payload[2];
    status = payload[3];
    nwkAddrOfInterest = (payload[4] << 8) | payload[5];
    length = payload[6];

    if (payload.size() != 7 + length) {
        return false;
    }

    simpleDesc.assign(payload.begin() + 7, payload.end());

    return true;
}

std::vector<uint8_t> ZigbeeDiscoveryActiveEpReq::serialize() const {
    return {
        static_cast<uint8_t>(dstAddr >> 8),
        static_cast<uint8_t>(dstAddr & 0xff),
        static_cast<uint8_t>(nwkAddrOfInterest >> 8),
        static_cast<uint8_t>(nwkAddrOfInterest & 0xff),
    };
}

bool ZigbeeDiscoveryActiveEpRsp::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() < 7) {
        return false;
    }

    srcAddr = (payload[0] << 8) | payload[1];
    seqNum = payload[2];
    status = payload[3];
    nwkAddrOfInterest = (payload[4] << 8) | payload[5];
    activeEpCount = payload[6];

    if (payload.size() != 7 + activeEpCount) {
        return false;
    }

    epList.assign(payload.begin() + 7, payload.end());

    return true;
}

std::vector<uint8_t> ZigbeeMgmtPermitJoinReq::serialize() const {
    return {
        static_cast<uint8_t>(dstAddr >> 8),
        static_cast<uint8_t>(dstAddr & 0xff),
        permitDuration,
        tcSignificance,
    };
}

bool ZigbeeNodesDevAnnceInd::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() != 11) {
        return false;
    }

    nwkAddr = (payload[0] << 8) | payload[1];
    std::reverse_copy(&payload[2], &payload[10], ieeeAddr);
    capability = payload[11];

    return true;
}

bool ZigbeeGetLocalNwkInfoRsp::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() != 23) {
        return false;
    }

    devType = payload[0];
    capability = payload[1];
    onANetwork = payload[2];
    panId = (payload[3] << 8) | payload[4];
    std::reverse_copy(&payload[5], &payload[13], extPanID);
    nwkAddr = (payload[13] << 8) | payload[14];
    std::reverse_copy(&payload[15], &payload[23], ieeeAddr);

    return true;
}

bool ZigbeeZclReportMsgRcv::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() < ZclRspHdr::SIZE + 3) {
        return false;
    }

    if (!header.deserialize(payload.subspan(0, ZclRspHdr::SIZE))) {
        return false;
    }

    clusterId = (payload[5] << 8) | payload[6];
    attrNum = payload[7];
    attrList.assign(payload.begin() + 8, payload.end());

    return true;
}
