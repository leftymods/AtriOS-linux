#include "mfg_commands.h"

using namespace telink;

bool MfgEui64Response::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(eui64)) {
        return false;
    }

    std::reverse_copy(payload.begin(), payload.end(), eui64);

    return true;
}

std::vector<uint8_t> MfgSetChannelRequest::serialize() const {
    return {
        channel,
    };
}

std::vector<uint8_t> MfgSetPowerRequest::serialize() const {
    return {
        static_cast<uint8_t>(power),
    };
}

std::vector<uint8_t> MfgSendPacketRequest::serialize() const {
    std::vector<uint8_t> payload;

    payload.push_back(data.size());
    payload.insert(payload.end(), data.begin(), data.end());

    return payload;
}

bool MfgReceivePacketIndication::deserialize(std::span<const uint8_t> payload) {
    if (payload.size() < 3) {
        return false;
    }

    lqi = payload[0];
    rssi = payload[1];
    length = payload[2];
    data.assign(payload.begin() + 3, payload.end());

    return true;
}
