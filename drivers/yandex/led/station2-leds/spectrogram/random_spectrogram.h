#pragma once

#include "spectrogram.h"

#include <yandex_io/libs/json_utils/json_utils.h>

#include <random>

class RandomSpectrogram: public YandexStation2Spectrogram {
public:
    RandomSpectrogram(
        size_t width, size_t height,
        const Json::Value& config,
        const Json::Value& presets,
        const std::string& extraData);

    std::string getExtraData() const override;

    size_t getBandCount() const override;

    float getMinFrequency() const override;

    float getMaxFrequency() const override;

    void draw(const std::vector<float>& bands) override;

    void reset() override;

private:
    struct Parameters {
        std::vector<std::string> presets;

        std::chrono::seconds interval = std::chrono::seconds::zero();
    };

    static Parameters loadParameters(const Json::Value& config);

    void pickSpectrogram();

private:
    const Parameters parameters_;
    const Json::Value presets_;
    int forcedPick_{-1};

    std::mt19937 gen_;

    int currentPick_;
    std::unique_ptr<YandexStation2Spectrogram> spectrogram_;

    std::chrono::steady_clock::time_point nextSwitchTime_;
};
