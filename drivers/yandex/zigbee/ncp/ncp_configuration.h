#pragma once

#include <yandex_io/libs/json_utils/json_utils.h>

namespace zigbee {

    struct NcpConfiguration {
        std::optional<uint16_t> manufacturerCode;
        std::optional<uint8_t> beaconJitterDuration;
        bool broadcastPermitJoining = false;

        static NcpConfiguration fromJson(const Json::Value& json);
    };

} // namespace zigbee
