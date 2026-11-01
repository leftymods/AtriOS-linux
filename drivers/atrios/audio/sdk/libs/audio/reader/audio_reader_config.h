#pragma once

#include <json/value.h>

#include <string>

namespace quasar {
    struct AudioReaderConfig {
        std::string deviceName;
        int cardNumber;
        int deviceNumber;
        int inRate;
        int micChannels;
        int spkChannels;
        int beamformingChannels;
        int periodSize;
        int periodCount;
        int sampleSize;
        std::string vqeTypeName;
        bool omniMode;
        std::string preset;

        AudioReaderConfig(const Json::Value& config, const std::string& deviceType);
    };
} // namespace quasar
