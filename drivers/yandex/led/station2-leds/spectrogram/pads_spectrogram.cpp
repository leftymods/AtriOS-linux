#include <smart_devices/libs/spectrogram/filters.h>
#include "pads_spectrogram.h"

PadsSpectrogram::PadsSpectrogram(
    size_t width, size_t height,
    const Json::Value& config)
    : YandexStation2Spectrogram(width, height)
    , parameters_(loadParameters(config))
{
    addFilter(createSmoothingSpectrumFilter(parameters_.bandSmoothingFactor));
    addFilter(createNormalizingSpectrumFilter(10));
}

size_t PadsSpectrogram::getBandCount() const {
    return parameters_.rows * parameters_.cols;
}

float PadsSpectrogram::getMinFrequency() const {
    return parameters_.frequencyMin;
}

float PadsSpectrogram::getMaxFrequency() const {
    return parameters_.frequencyMax;
}

void PadsSpectrogram::draw(const std::vector<float>& bands) {
    clear();

    for (size_t ry = 0; ry < parameters_.rows; ry++) {
        for (size_t rx = 0; rx < parameters_.cols; rx++) {
            const size_t b = rx * 4 + ry;
            const float color = std::pow(std::clamp(bands[b], 0.0f, 1.0f), parameters_.brightnessGamma);

            const size_t cx = parameters_.xOffset + rx * parameters_.padSpacing;
            const size_t cy = parameters_.yOffset + ry * parameters_.padSpacing;

            const float center = (float)(parameters_.padSize - 1) / 2;

            for (size_t px = 0; px < parameters_.padSize; px++) {
                for (size_t py = 0; py < parameters_.padSize; py++) {
                    float dx = center - px;
                    float dy = center - py;

                    float d = std::sqrt(dx * dx + dy * dy);

                    size_t x = cx + dx;
                    size_t y = cy + dy;

                    float distanceFactor = std::pow(1.0f - std::min(1.0f, d / (center + 1)), parameters_.brightnessFading);

                    pixel(x, y) += color * distanceFactor;
                }
            }
        }
    }
}

PadsSpectrogram::Parameters PadsSpectrogram::loadParameters(const Json::Value& config) {
    Parameters parameters;

    if (const auto& frequencyMin = config["frequencyMin"]) {
        parameters.frequencyMin = std::max(0.0f, frequencyMin.asFloat());
    }
    if (const auto& frequencyMax = config["frequencyMax"]) {
        parameters.frequencyMax = std::max(0.0f, frequencyMax.asFloat());
    }
    if (const auto& bandSmoothingFactor = config["bandSmoothingFactor"]) {
        parameters.bandSmoothingFactor = std::clamp(bandSmoothingFactor.asFloat(), 0.0f, 1.0f);
    }
    if (const auto& rows = config["rows"]) {
        parameters.rows = rows.asUInt();
    }
    if (const auto& cols = config["cols"]) {
        parameters.cols = cols.asUInt();
    }
    if (const auto& xOffset = config["xOffset"]) {
        parameters.xOffset = xOffset.asUInt();
    }
    if (const auto& yOffset = config["yOffset"]) {
        parameters.yOffset = yOffset.asUInt();
    }
    if (const auto& padSize = config["padSize"]) {
        parameters.padSize = padSize.asUInt();
    }
    if (const auto& padSpacing = config["padSpacing"]) {
        parameters.padSpacing = padSpacing.asUInt();
    }
    if (const auto& brightnessFading = config["brightnessFading"]) {
        parameters.brightnessFading = std::max(0.0f, brightnessFading.asFloat());
    }
    if (const auto& brightnessGamma = config["brightnessGamma"]) {
        parameters.brightnessGamma = std::max(0.0f, brightnessGamma.asFloat());
    }

    return parameters;
}
