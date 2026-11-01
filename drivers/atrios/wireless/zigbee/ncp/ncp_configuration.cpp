#include "ncp_configuration.h"

using namespace zigbee;

NcpConfiguration NcpConfiguration::fromJson(const Json::Value& json) {
    NcpConfiguration configuration = {};

    if (const auto manufacturerCode = json["manufacturerCode"]) {
        configuration.manufacturerCode = manufacturerCode.asUInt();
    }

    if (const auto beaconJitterDuration = json["beaconJitterDuration"]) {
        configuration.beaconJitterDuration = beaconJitterDuration.asUInt();
    }

    if (const auto broadcastPermitJoining = json["broadcastPermitJoining"]) {
        configuration.broadcastPermitJoining = broadcastPermitJoining.asBool();
    }

    return configuration;
}
