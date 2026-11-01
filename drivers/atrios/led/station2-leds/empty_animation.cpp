#include "empty_animation.h"

namespace {
    constexpr auto frameLength = std::chrono::seconds(1);
} // namespace

EmptyAnimation::EmptyAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice)
    : Animation(ledDevice)
    , ledDevice_(std::move(ledDevice))
    , screen_({{YandexStationFrontalController::WIDTH,
                YandexStationFrontalController::HEIGHT},
               Frame(YandexStationFrontalController::HEIGHT * YandexStationFrontalController::WIDTH, {BGR_BLACK, BGR_BLACK, BGR_BLACK})})
{
}

void EmptyAnimation::drawCurrentFrame() {
    if (auto ledController = ledDevice_.lock()) {
        ledController->setLastAnimationName("empty");
        ledController->drawBuffer(screen_.data);
    }
}

bool EmptyAnimation::finished() const {
    if (endOfAnimationTp_.has_value()) {
        return (currentTime_ > *endOfAnimationTp_);
    }
    return false;
}

Animation::TimePoint EmptyAnimation::getEndOfFrameTimePoint() const {
    return endOfFrame_;
}

std::string EmptyAnimation::getName() const {
    return "empty";
}

void EmptyAnimation::resetAnimation() {
}

void EmptyAnimation::setFinishedTimepoint(std::chrono::steady_clock::time_point tp) {
    endOfAnimationTp_ = tp;
}

void EmptyAnimation::updateTime(Animation::TimePoint timePoint) {
    currentTime_ = timePoint;
    // simulate constant frame rate
    while (timePoint >= endOfFrame_) {
        endOfFrame_ += frameLength;
    }
}

void EmptyAnimation::startAnimationFrom(Animation::TimePoint timePoint) {
    endOfFrame_ = timePoint + frameLength;
}

std::chrono::nanoseconds EmptyAnimation::getLength() const {
    return frameLength;
}
