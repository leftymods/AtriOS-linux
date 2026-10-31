#pragma once

#include "spectrogram.h"

#include <yandex_io/libs/json_utils/json_utils.h>

class PadsSpectrogram: public YandexStation2Spectrogram {
public:
    PadsSpectrogram(
        size_t width, size_t height,
        const Json::Value& config);

    size_t getBandCount() const override;

    float getMinFrequency() const override;

    float getMaxFrequency() const override;

    void draw(const std::vector<float>& bands) override;

private:
    struct Parameters {
        float frequencyMin = 40.0f;
        float frequencyMax = 3000.0f;

        float bandSmoothingFactor = 0.8f;

        size_t rows = 4;
        size_t cols = 6;

        size_t xOffset = 2;
        size_t yOffset = 2;

        size_t padSize = 3;
        size_t padSpacing = 4;

        float brightnessFading = 0.0f;

        float brightnessGamma = 1.0f;
    };

    static Parameters loadParameters(const Json::Value& config);

private:
    const Parameters parameters_;
};
