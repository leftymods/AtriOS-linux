#include "station2_volume_pattern.h"

#include <yandex_io/libs/logging/logging.h>

#include <cmath>

using namespace quasar;

Station2VolumePattern::Station2VolumePattern(std::shared_ptr<LedPattern> basePattern)
    : LedPattern(basePattern->getLedCount(), "volume", basePattern->getDevice())
    , basePattern_(basePattern)
{
    if (basePattern_->empty()) {
        throw std::runtime_error("Cannot create volume LED pattern with empty base pattern");
    }
}

void Station2VolumePattern::updateVolume(double volume) {
    std::lock_guard<std::mutex> lock(mutex_);

    started_ = false;
    currentFrame_ = 0;
    endOfDuration_ = std::chrono::steady_clock::time_point::max();

    if (volume < 0) {
        volume = 0;
    }
    if (volume > 1) {
        volume = 1;
    }

    frames.clear();
    std::vector<LedFrame> baseFrames = basePattern_->getFrames();

    frames.push_back(baseFrames[round(volume * (baseFrames.size() - 1))]);
    frames[0].delayMs = 500;
    setSmooth(50);
    addSmoothFrame(getIdleFrame(500, ledCount_));
}

bool Station2VolumePattern::finished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return LedPattern::finished();
}

Animation::TimePoint Station2VolumePattern::getEndOfFrameTimePoint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return LedPattern::getEndOfFrameTimePoint();
}

LedFrame Station2VolumePattern::getCurrentFrame() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return LedPattern::getCurrentFrame();
}
