#pragma once

#include "spectrogram.h"

#include <yandex_io/libs/json_utils/json_utils.h>

class WaveSpectrogram: public YandexStation2Spectrogram {
public:
    WaveSpectrogram(
        size_t width, size_t height,
        const Json::Value& config);

    size_t getBandCount() const override;

    float getMinFrequency() const override;

    float getMaxFrequency() const override;

    void draw(const std::vector<float>& bands) override;

private:
    enum class FillType {
        SOLID = 0,
        BARS
    };

    enum class OriginType {
        CENTER = 0,
        BOTTOM
    };

    struct Parameters {
        float frequencyMin = 40.0f;
        float frequencyMax = 3000.0f;

        size_t bandCount = 18;
        float bandSmoothingFactor = 0.8f;

        size_t splineOrder = 10;

        float barWidth = 0.5f;

        float brightnessFading = 0.25f;

        FillType fill = FillType::SOLID;

        OriginType origin = OriginType::CENTER;
    };

    static Parameters loadParameters(const Json::Value& config);

private:
    const Parameters parameters_;
};
