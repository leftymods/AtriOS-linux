#pragma once

#include <yandex_io/modules/leds/led_patterns/led_pattern.h>

#include <mutex>

class Station2VolumePattern: public quasar::LedPattern {
public:
    explicit Station2VolumePattern(std::shared_ptr<LedPattern> basePattern);

    void updateVolume(double volume);

    bool finished() const override;

    TimePoint getEndOfFrameTimePoint() const override;

    quasar::LedFrame getCurrentFrame() const override;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<LedPattern> basePattern_;
};
