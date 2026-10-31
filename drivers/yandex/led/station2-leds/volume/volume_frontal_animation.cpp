#include "volume_frontal_animation.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

VolumeFrontalAnimation::VolumeFrontalAnimation(const std::string& gifFilename,
                                               std::weak_ptr<YandexStationFrontalController> ledDevice)
    : GifAnimation(
          std::move(ledDevice),
          gifFilename)
{
}

FrameBufferOutputFrame VolumeFrontalAnimation::getCurrentFrame() const {
    std::lock_guard<std::mutex> lock(frameQueueMutex_);
    return frameQueue_.front();
}

bool VolumeFrontalAnimation::finished() const {
    std::lock_guard<std::mutex> lock(frameQueueMutex_);
    return frameQueue_.empty();
}

void VolumeFrontalAnimation::resetAnimation() {
    std::lock_guard<std::mutex> lock(frameQueueMutex_);
    decltype(frameQueue_) tmpFrameQueue_;
    decltype(durationQueue_) tmpDurationQueue_;
    std::swap(frameQueue_, tmpFrameQueue_);
    std::swap(durationQueue_, tmpDurationQueue_);
}

bool equalsNear1(double volume1, double volume2) {
    return std::fabs(volume1 - volume2) < std::numeric_limits<double>::epsilon();
}

void VolumeFrontalAnimation::updateAnimation(double volume) {
    Y_VERIFY(volume <= 1.0f && volume >= 0.0f);
    std::lock_guard<std::mutex> lock(frameQueueMutex_);
    double volumeFrame = volume * (frames_.size() - 1);
    static constexpr double pi = 3.1415926535;
    // slow down animation around "whole" digits
    double slowedDown = volumeFrame + 1 / pi * 25 * sin(pi + volumeFrame * pi / 25);
    int targetLevel = int(slowedDown);
    // show exact digits when otherwise it would skip around e.g. on 499 show 500
    const int targetLevelExtra = ((targetLevel % (frames_.size() / 5)) == (frames_.size() / 5 - 1)) ? 1 : 0;
    int correctedLevel = targetLevel + targetLevelExtra;
    if (correctedLevel > int(frames_.size() - 1)) {
        correctedLevel = frames_.size() - 1;
    }

    if (abs(volume_ - volume) < 0.02 && !equalsNear1(volume_, volume)) {
        // this is volume wheel "button" rotation
        int direction = (volume_ > volume) ? -1 : 1;
        /**
         * represents responsiveness|smoothness tradeoff
         * 0 is minimum smoothness, immediate reaction on wheel
         * 1 is very responsive, but still smooth enough (2 frames per "tick")
         */
        constexpr int VOLUME_UPDATE_SMOOTHNESS = 0;
        for (int i = VOLUME_UPDATE_SMOOTHNESS; i >= 0; --i) {
            int index = direction * i + correctedLevel;
            if (index >= 0 && index < int(frames_.size())) {
                frameQueue_.push(frames_[index]);
                durationQueue_.push(durationCentiSeconds_[index]);
            }
        }
    } else {
        // this is not volume wheel "button" rotation
        // abrupt volume changes do not need smooth animation
        frameQueue_.push(frames_[correctedLevel]);
        durationQueue_.push(durationCentiSeconds_[correctedLevel]);
    }
    endOfFrame_ = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(1);

    updateVolume(volume);
}

void VolumeFrontalAnimation::updateVolume(double volume) {
    volume_ = volume;
}

void VolumeFrontalAnimation::updateTime(Animation::TimePoint timePoint) {
    std::lock_guard<std::mutex> lock(frameQueueMutex_);
    while (timePoint >= endOfFrame_ && !frameQueue_.empty()) {
        auto duration = durationQueue_.front();
        endOfFrame_ = endOfFrame_ + std::chrono::milliseconds(duration);
        frameQueue_.pop();
        durationQueue_.pop();
    }
}
