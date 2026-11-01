#pragma once

#include "spectrogram.h"

#include <yandex_io/libs/json_utils/json_utils.h>

class BricksSpectrogram: public YandexStation2Spectrogram {
public:
    BricksSpectrogram(
        size_t width, size_t height,
        const Json::Value& config);

    size_t getBandCount() const override;

    float getMinFrequency() const override;

    float getMaxFrequency() const override;

    void draw(const std::vector<float>& bands) override;

    void reset() override;

private:
    struct Parameters {
        float frequencyMin = 40.0f;
        float frequencyMax = 3000.0f;

        size_t brickWidth = 2;
        size_t brickHeight = 1;

        float bandSmoothingFactor = 0.8f;

        float falloffSpeed = 0.0f;

        float brickColor = 1.0f;
        float falloffColor = 0.75f;

        float brightnessFading = 0.0f;

        bool bottomRow = true;
    };

    void drawBrick(size_t bx, size_t by, float color);

    static Parameters loadParameters(const Json::Value& config);

private:
    const Parameters parameters_;

    std::vector<float> falloff_;
};
