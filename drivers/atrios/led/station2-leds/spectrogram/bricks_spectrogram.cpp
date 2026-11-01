#include "bricks_spectrogram.h"
#include <smart_devices/libs/spectrogram/filters.h>

BricksSpectrogram::BricksSpectrogram(
    size_t width, size_t height,
    const Json::Value& config)
    : YandexStation2Spectrogram(width, height)
    , parameters_(loadParameters(config))
{
    addFilter(createSmoothingSpectrumFilter(parameters_.bandSmoothingFactor));
    addFilter(createNormalizingSpectrumFilter(10));

    falloff_.resize(getBandCount());
}

size_t BricksSpectrogram::getBandCount() const {
    return (getWidth() + 1) / (parameters_.brickWidth + 1);
}

float BricksSpectrogram::getMinFrequency() const {
    return parameters_.frequencyMin;
}

float BricksSpectrogram::getMaxFrequency() const {
    return parameters_.frequencyMax;
}

void BricksSpectrogram::drawBrick(size_t bx, size_t by, float color) {
    size_t x0 = (parameters_.brickWidth + 1) * bx;
    size_t y0 = (parameters_.brickHeight + 1) * by;

    for (size_t x = x0; x < x0 + parameters_.brickWidth; x++) {
        for (size_t y = y0; y < y0 + parameters_.brickHeight; y++) {
            pixel(x, y) = color;
        }
    }
}

void BricksSpectrogram::draw(const std::vector<float>& bands) {
    const size_t maxHeight = (getHeight() + 1) / (parameters_.brickHeight + 1);

    clear();

    for (size_t b = 0; b < bands.size(); b++) {
        float value = std::clamp(bands[b], 0.0f, 1.0f);

        falloff_[b] = std::max(value, falloff_[b] - parameters_.falloffSpeed / 25);

        size_t height = std::round(value * (maxHeight - 1));
        if (parameters_.bottomRow) {
            height = std::max<size_t>(height, 1);
        }

        size_t falloffHeight = std::round(falloff_[b] * (maxHeight - 1));

        for (size_t h = 0; h < height; h++) {
            float color = parameters_.brickColor * std::pow(1.0f - (float)h / height, parameters_.brightnessFading);

            drawBrick(b, h, color);
        }

        if (parameters_.falloffSpeed > 0.0f) {
            drawBrick(b, falloffHeight, parameters_.falloffColor);
        }
    }
}

void BricksSpectrogram::reset() {
    resetFilters();

    std::fill(falloff_.begin(), falloff_.end(), 0.0f);
}

BricksSpectrogram::Parameters BricksSpectrogram::loadParameters(const Json::Value& config) {
    Parameters parameters;

    if (const auto& frequencyMin = config["frequencyMin"]) {
        parameters.frequencyMin = std::max(0.0f, frequencyMin.asFloat());
    }
    if (const auto& frequencyMax = config["frequencyMax"]) {
        parameters.frequencyMax = std::max(0.0f, frequencyMax.asFloat());
    }
    if (const auto& brickWidth = config["brickWidth"]) {
        parameters.brickWidth = brickWidth.asUInt();
    }
    if (const auto& brickHeight = config["brickHeight"]) {
        parameters.brickHeight = brickHeight.asUInt();
    }
    if (const auto& bandSmoothingFactor = config["bandSmoothingFactor"]) {
        parameters.bandSmoothingFactor = std::clamp(bandSmoothingFactor.asFloat(), 0.0f, 1.0f);
    }
    if (const auto& falloffSpeed = config["falloffSpeed"]) {
        parameters.falloffSpeed = std::max(0.0f, falloffSpeed.asFloat());
    }
    if (const auto& brickColor = config["brickColor"]) {
        parameters.brickColor = std::clamp(brickColor.asFloat(), 0.0f, 1.0f);
    }
    if (const auto& falloffColor = config["falloffColor"]) {
        parameters.falloffColor = std::clamp(falloffColor.asFloat(), 0.0f, 1.0f);
    }
    if (const auto& brightnessFading = config["brightnessFading"]) {
        parameters.brightnessFading = std::max(0.0f, brightnessFading.asFloat());
    }
    if (const auto& bottomRow = config["bottomRow"]) {
        parameters.bottomRow = bottomRow.asBool();
    }
    return parameters;
}
