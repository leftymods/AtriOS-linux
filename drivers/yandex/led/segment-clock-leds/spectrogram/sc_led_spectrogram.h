#pragma once

#include <smart_devices/libs/spectrogram/spectrogram.h>
#include <smart_devices/libs/segment_clock_leds/sc_led_frame.h>

#include <vector>

class ScLedSpectrogram: public Spectrogram {
public:
    ScLedSpectrogram();

    ScLedFrame& frame();

    size_t getBandCount() const override;
    float getMinFrequency() const override;
    float getMaxFrequency() const override;
    void draw(const std::vector<float>& bands) override;
    void clear() override;

protected:
    ScLedFrame frame_;
};
