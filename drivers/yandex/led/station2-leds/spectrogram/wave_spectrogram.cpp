#include <smart_devices/libs/spectrogram/filters.h>
#include "wave_spectrogram.h"

#include <cmath>

namespace {

    float bspline(float x, const std::vector<float>& c, int p) {
        int k = std::floor(x);

        std::vector<float> d(p + 1);

        for (int i = 0; i <= p; i++) {
            int j = i + k - (p / 2);
            if (j < 0 || j >= (int)c.size()) {
                continue;
            }
            d[i] = c[j];
        }

        for (int r = 1; r <= p; r++) {
            for (int j = p; j >= r; j--) {
                float a = (x - (float)(j + k - p)) / (p - r + 1);
                d[j] = (1.0f - a) * d[j - 1] + a * d[j];
            }
        }

        return d[p];
    }

} // namespace

WaveSpectrogram::WaveSpectrogram(
    size_t width, size_t height,
    const Json::Value& config)
    : YandexStation2Spectrogram(width, height)
    , parameters_(loadParameters(config))
{
    addFilter(createSmoothingSpectrumFilter(parameters_.bandSmoothingFactor));
    addFilter(createNormalizingSpectrumFilter(10));
}

size_t WaveSpectrogram::getBandCount() const {
    return parameters_.bandCount;
}

float WaveSpectrogram::getMinFrequency() const {
    return parameters_.frequencyMin;
}

float WaveSpectrogram::getMaxFrequency() const {
    return parameters_.frequencyMax;
}

void WaveSpectrogram::draw(const std::vector<float>& bands) {
    const size_t width = getWidth();
    const size_t height = (parameters_.origin == OriginType::BOTTOM) ? getHeight() : getHeight() / 2;
    const size_t sample = (parameters_.fill == FillType::BARS) ? 1 : 8;

    std::vector<float> controlPoints(bands.size());
    for (size_t i = 0; i < bands.size(); i++) {
        controlPoints[i] = std::clamp(bands[i] * height, parameters_.barWidth, (float)height) * sample;
    }

    clear();

    for (size_t x = 0; x < width * sample; x++) {
        if (parameters_.fill == FillType::BARS && x % 2 == 1) {
            continue;
        }

        float band = (float)(controlPoints.size() - 1) * (float)x / (width * sample);

        float hf = bspline(band, controlPoints, parameters_.splineOrder);

        size_t h = std::round(hf);

        if (parameters_.barWidth == 0.0f && (h < sample / 2)) {
            continue;
        }

        for (size_t y = 0; y < h; y++) {
            float color = std::pow(1.0f - (float)y / h, parameters_.brightnessFading) / (sample * sample);

            if (parameters_.origin == OriginType::CENTER) {
                pixel(x / sample, height + y / sample) += color;
                if (y / sample) {
                    pixel(x / sample, height - y / sample) += color;
                }
            } else {
                pixel(x / sample, y / sample) += color;
            }
        }
    }
}

WaveSpectrogram::Parameters WaveSpectrogram::loadParameters(const Json::Value& config) {
    Parameters parameters;

    if (const auto& frequencyMin = config["frequencyMin"]) {
        parameters.frequencyMin = std::max(0.0f, frequencyMin.asFloat());
    }
    if (const auto& frequencyMax = config["frequencyMax"]) {
        parameters.frequencyMax = std::max(0.0f, frequencyMax.asFloat());
    }
    if (const auto& bandCount = config["bandCount"]) {
        parameters.bandCount = bandCount.asUInt();
    }
    if (const auto& bandSmoothingFactor = config["bandSmoothingFactor"]) {
        parameters.bandSmoothingFactor = std::clamp(bandSmoothingFactor.asFloat(), 0.0f, 1.0f);
    }
    if (const auto& splineOrder = config["splineOrder"]) {
        parameters.splineOrder = splineOrder.asUInt();
    }
    if (const auto& barWidth = config["barWidth"]) {
        parameters.barWidth = std::max(0.0f, barWidth.asFloat());
    }
    if (const auto& brightnessFading = config["brightnessFading"]) {
        parameters.brightnessFading = std::max(0.0f, brightnessFading.asFloat());
    }
    if (const auto& fill = config["fill"]) {
        if (fill.asString() == "bars") {
            parameters.fill = FillType::BARS;
        }
    }
    if (const auto& origin = config["origin"]) {
        if (origin.asString() == "bottom") {
            parameters.origin = OriginType::BOTTOM;
        }
    }

    return parameters;
}
