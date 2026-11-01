#include "audio_config.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>

using namespace AudioSender;

AudioConfig::AudioConfig(const Json::Value& config) {
    if (config.isMember("dumpingChannels")) {
        const auto& dumpingChannels = config["dumpingChannels"];
        for (auto iter = dumpingChannels.begin(); iter != dumpingChannels.end(); ++iter) {
            dumpingChannelNames.insert(iter->asString());
        }
    }
    spotterChannelName = quasar::tryGetString(config, "spotterChannelName", "vqe");
    playerChannelName = quasar::tryGetString(config, "playerChannelName", "vqe");
    historyDurationSec = quasar::tryGetInt(config, "historySeconds", 10);
}

const std::unordered_set<std::string>& AudioConfig::getDumpingChannelNames() const {
    return dumpingChannelNames;
}

const std::string& AudioConfig::getSpotterChannelName() const {
    return spotterChannelName;
}

const std::string& AudioConfig::getPlayerChannelName() const {
    return playerChannelName;
}

int AudioConfig::getHistoryDurationSec() const {
    return historyDurationSec;
}
