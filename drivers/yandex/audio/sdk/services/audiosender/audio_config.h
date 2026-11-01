#pragma once

#include <json/json.h>

#include <memory>
#include <unordered_set>

namespace AudioSender {

    class AudioConfig {
    public:
        using SharedPtr = std::shared_ptr<AudioConfig>;

        explicit AudioConfig(const Json::Value& config);

        const std::unordered_set<std::string>& getDumpingChannelNames() const;
        const std::string& getSpotterChannelName() const;
        const std::string& getPlayerChannelName() const;
        int getHistoryDurationSec() const;

    private:
        std::unordered_set<std::string> dumpingChannelNames;
        std::string spotterChannelName;
        std::string playerChannelName;
        int historyDurationSec;
    };

} // namespace AudioSender
