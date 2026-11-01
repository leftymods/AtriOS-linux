#include "progress_update_gif.h"

#include <yandex_io/libs/logging/logging.h>

ProgressUpdateGif::ProgressUpdateGif(std::weak_ptr<YandexStationFrontalController> ledDevice, const std::string& imageFilePath)
    : GifAnimation(std::move(ledDevice), imageFilePath)
{
}

void ProgressUpdateGif::setProgress(double progressPercents)
{
    std::lock_guard<std::mutex> lockGuard(mutex_);
    const int percentFrame = getFrameCount() * progressPercents / 100;
    currentFrame_ = std::min(percentFrame, std::max(getFrameCount() - 1, 0));
    endOfFrame_ = startOfCurrentFrame_ + std::chrono::milliseconds();
}

bool ProgressUpdateGif::finished() const {
    return false;
}

void ProgressUpdateGif::updateTime(Animation::TimePoint timePoint) {
    std::lock_guard<std::mutex> lockGuard(mutex_);
    // simulate constant frame rate
    while (timePoint >= endOfFrame_) {
        endOfFrame_ = endOfFrame_ + std::chrono::milliseconds(durationCentiSeconds_[currentFrame_]);
    }
}

void ProgressUpdateGif::startAnimationFrom(Animation::TimePoint timePoint) {
    std::lock_guard<std::mutex> lockGuard(mutex_);
    GifAnimation::startAnimationFrom(timePoint);
    startOfCurrentFrame_ = timePoint;
    endOfFrame_ = timePoint + std::chrono::milliseconds(durationCentiSeconds_[currentFrame_]);
}

std::chrono::nanoseconds ProgressUpdateGif::getLength() const {
    return std::chrono::nanoseconds();
}

FrameBufferOutputFrame ProgressUpdateGif::getCurrentFrame() const {
    std::lock_guard<std::mutex> lockGuard(mutex_);
    return frames_[currentFrame_];
}
