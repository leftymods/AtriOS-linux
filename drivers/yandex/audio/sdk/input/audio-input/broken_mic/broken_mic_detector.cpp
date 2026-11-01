#include "broken_mic_detector.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <limits>
#include <math.h>

using namespace quasar;

namespace YandexIO {

    BrokenMicDetector::Config BrokenMicDetector::Config::parseFromJson(const Json::Value& json) {
        Config config;

        config.rmsRatioThreshold = tryGetDouble(json, "rmsRatioThreshold", 0.8);
        config.rmsRatioExceededCount = tryGetInt(json, "rmsRatioExceededCount", 25);

        return config;
    }

    bool BrokenMicDetector::Config::operator==(const Config& other) const {
        return rmsRatioThreshold == other.rmsRatioThreshold &&
               rmsRatioExceededCount == other.rmsRatioExceededCount;
    }

    bool BrokenMicDetector::Config::operator!=(const Config& other) const {
        return !(*this == other);
    }

    std::string BrokenMicDetector::Config::toString(const BrokenMicDetector::Config& config) {
        std::stringstream ss;
        ss << "RmsRatioThreshold: " << config.rmsRatioThreshold << " RmsRatioExceededCount: " << config.rmsRatioExceededCount;
        return ss.str();
    }

    BrokenMicDetector::BrokenMicDetector(const Config& config)
        : config(config)
    {
        resetNextCheckPoint();
    }

    const BrokenMicDetector::Config& BrokenMicDetector::getConfig() const {
        return config;
    }

    BrokenMicDetector::Status BrokenMicDetector::catchBrokenMics(
        const AudioDevice::ChannelToChunk& chunk, AudioDevice::ChannelToChunk& micSample, std::vector<float>& rmsRatios) {
        if (const auto status = checkActive(); status != Status::NeedMoreData) {
            return status;
        }

        rmsRatios.clear();
        micSample.clear();

        // calc rms values
        std::vector<float> rmsValues;
        for (const auto& channelToChunk : chunk) {
            if (isRawChannel(channelToChunk.first)) {
                rmsValues.push_back(calcRms(channelToChunk.second.data));
            }
        }

        // detect anomaly rms
        for (size_t i = 0; i < rmsValues.size(); ++i) {
            for (size_t j = i + 1; j < rmsValues.size(); ++j) {
                const float ratio = calcRatio(rmsValues[i], rmsValues[j]);
                if (ratio >= config.rmsRatioThreshold) {
                    appendBrokenMicsSample(chunk);
                    brokenMicRmsRatios.push_back(ratio);

                    if (brokenMicRmsRatios.size() >= config.rmsRatioExceededCount) {
                        micSample.swap(brokenMicSample);
                        rmsRatios.swap(brokenMicRmsRatios);

                        scheduleNextCheckPoint();
                        return Status::Detected;
                    } else {
                        // required count of anomaly chunks not reached
                        return Status::NeedMoreData;
                    }
                }
            }
        }

        brokenMicSample.clear();
        brokenMicRmsRatios.clear();

        return Status::NeedMoreData;
    }

    float BrokenMicDetector::calcRatio(float v1, float v2) {
        const float diff = fabsf(v1 - v2);
        const float maxValue = std::max(v1, v2);

        if (maxValue < std::numeric_limits<float>::epsilon()) {
            return 0.f;
        }

        return diff / maxValue;
    }

    float BrokenMicDetector::calcRms(const std::vector<int16_t>& data) {
        Y_VERIFY(!data.empty());

        uint64_t squareSum = 0;
        for (const auto& value : data) {
            const uint64_t square = value * value;
            squareSum += square;
        }

        const uint64_t meanSquare = squareSum / data.size();

        return sqrt(meanSquare);
    }

    void BrokenMicDetector::resetNextCheckPoint() {
        nextCheckPointTime = Clock::now() + std::chrono::seconds(5);
    }

    void BrokenMicDetector::startForcedRun() {
        YIO_LOG_INFO("Start forced broken mic detecting");
        forceRunning = true;
    }

    void BrokenMicDetector::stopForcedRun() {
        YIO_LOG_INFO("Stop forced broken mic detecting");
        forceRunning = false;
        scheduleNextCheckPoint();
    }

    bool BrokenMicDetector::isForcedRun() const {
        return forceRunning;
    }

    BrokenMicDetector::Status BrokenMicDetector::checkActive() {
        if (forceRunning) {
            return Status::NeedMoreData;
        }

        // Check mics every 30 minutes during 1 minute
        const auto now = Clock::now();

        if (nextCheckPointTime > now) {
            return Status::NotActive;
        }

        if (nextCheckPointTime + std::chrono::minutes(1) < now) {
            scheduleNextCheckPoint();
            return Status::NotDetected;
        }

        return Status::NeedMoreData;
    }

    void BrokenMicDetector::scheduleNextCheckPoint() {
        brokenMicSample.clear();
        brokenMicRmsRatios.clear();

        nextCheckPointTime = Clock::now() + std::chrono::minutes(30);
    }

    void BrokenMicDetector::appendBrokenMicsSample(const AudioDevice::ChannelToChunk& chunk) {
        for (const auto& kv : chunk) {
            const auto& channelName = kv.first;

            if (isRawChannel(channelName)) {
                auto& newData = kv.second.data;
                auto& existingData = brokenMicSample[channelName].data;

                existingData.insert(existingData.end(), newData.begin(), newData.end());
            }
        }
    }

    bool BrokenMicDetector::isRawChannel(const std::string& name) {
        return name.rfind("raw_mic", 0) == 0;
    }

} // namespace YandexIO
