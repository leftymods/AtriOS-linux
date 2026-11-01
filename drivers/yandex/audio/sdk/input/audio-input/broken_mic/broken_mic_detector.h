#pragma once

#include <yandex_io/modules/audio_input/audio_device/audio_device.h>

#include <json/json.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace YandexIO {

    class BrokenMicDetector {
    public:
        struct Config {
            float rmsRatioThreshold;
            size_t rmsRatioExceededCount;

            bool operator==(const Config& other) const;
            bool operator!=(const Config& other) const;

            static Config parseFromJson(const Json::Value& json);
            static std::string toString(const Config& config);
        };

        enum class Status {
            Detected,
            NotDetected,
            NeedMoreData,
            NotActive
        };

        using Clock = std::chrono::steady_clock;
        using Timestamp = std::chrono::time_point<Clock>;

    public:
        explicit BrokenMicDetector(const Config& config);

        const Config& getConfig() const;

        Status catchBrokenMics(const AudioDevice::ChannelToChunk& chunk,
                               AudioDevice::ChannelToChunk& micSample,
                               std::vector<float>& rmsRatios);

        void resetNextCheckPoint();
        void startForcedRun();
        void stopForcedRun();
        bool isForcedRun() const;

    private:
        static float calcRatio(float v1, float v2);
        static float calcRms(const std::vector<int16_t>& data);

        Status checkActive();
        void scheduleNextCheckPoint();
        void appendBrokenMicsSample(const AudioDevice::ChannelToChunk& chunk);

        static bool isRawChannel(const std::string& name);

    private:
        const Config config;

        Timestamp nextCheckPointTime;
        std::vector<float> brokenMicRmsRatios;
        AudioDevice::ChannelToChunk brokenMicSample;
        bool forceRunning{false};
    };

} // namespace YandexIO
