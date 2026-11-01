#pragma once

#include <yandex_io/libs/equalizer_config/equalizer_config.h>
#include <yandex_io/sdk/audio_source/defines.h>

#include <json/json.h>

#include <vector>
#include <optional>

namespace YandexIO {
    class SmartEqualizer {
    public:
        SmartEqualizer(const Json::Value& config);
        ~SmartEqualizer();

        void setConfig(const Json::Value& config);

        bool initialized() const;

        void processAudioData(const ChannelsData& data);

        std::optional<EqualizerConfig> getConfig();

        void onOmniMicChange(int omniMic);

        void setUserEqualizerConfig(const EqualizerConfig& config);

        struct Rms {
            float mic;
            float feedback;
        };
        Rms getMicAndFeedbackRms();

        std::string toString();

    private:
        void applyConfig(const Json::Value& config);

    private:
        const Json::Value defaultConfig_;
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace YandexIO
