#include "sc_led_spectrogram.h"

#include <smart_devices/libs/spectrogram/filters.h>

#include <algorithm>

namespace {
    constexpr uint8_t MIN_BRIGHTNESS = 25;
} // namespace

ScLedSpectrogram::ScLedSpectrogram()
{
    addFilter(createSmoothingSpectrumFilter(0.8f));
    addFilter(createNormalizingSpectrumFilter(10));
    clear();
}

ScLedFrame& ScLedSpectrogram::frame()
{
    return frame_;
}

size_t ScLedSpectrogram::getBandCount() const {
    return 4;
}

float ScLedSpectrogram::getMaxFrequency() const {
    return 7000.0f;
}

float ScLedSpectrogram::getMinFrequency() const {
    return 40.0f;
}

void ScLedSpectrogram::draw(const std::vector<float>& bands)
{
    frame_ = ScLedFrame::makeEmptyFrame();
    for (size_t i = 0; i < bands.size(); ++i) {
        std::string pos = std::to_string(i + 1);

        int value = std::clamp(bands[i], 0.0f, 1.0f) * (3 * ScLedFrame::brightnessOn - MIN_BRIGHTNESS);
        frame_.elements[{"D", pos}] = std::min((int)ScLedFrame::brightnessOn, value + MIN_BRIGHTNESS);
        frame_.elements[{"G", pos}] = std::min(std::max(value - (ScLedFrame::brightnessOn - MIN_BRIGHTNESS), 0), (int)ScLedFrame::brightnessOn);
        frame_.elements[{"A", pos}] = std::max(value - (2 * ScLedFrame::brightnessOn - MIN_BRIGHTNESS), 0);
    }
}

void ScLedSpectrogram::clear()
{
    frame_ = ScLedFrame::makeEmptyFrame();
    for (size_t i = 0; i < 4; ++i) {
        std::string pos = std::to_string(i + 1);
        frame_.elements[{"D", pos}] = MIN_BRIGHTNESS;
    }
}
