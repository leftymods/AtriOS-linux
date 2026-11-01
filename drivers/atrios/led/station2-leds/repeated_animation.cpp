#include "repeated_animation.h"
#include <yandex_io/libs/logging/logging.h>

RepeatedAnimation::RepeatedAnimation(std::shared_ptr<Animation> animation, int repeatCount)
    : Animation(animation->getDevice())
    , animation_(std::move(animation))
{
    if (repeatCount == RepeatedAnimation::INFINITE) {
        repeatCount_ = 0;
        infinite_ = true;

    } else if (repeatCount >= 0) {
        repeatCount_ = repeatCount;
        infinite_ = false;

    } else {
        throw std::runtime_error("Unexpected repeatCount: " + std::to_string(repeatCount));
    }
}

void RepeatedAnimation::drawCurrentFrame() {
    if (!animation_->finished()) {
        animation_->drawCurrentFrame();
    }
}

bool RepeatedAnimation::finished() const {
    return animation_->finished() && !infinite_ && repeatCounter_ >= repeatCount_;
}

Animation::TimePoint RepeatedAnimation::getEndOfFrameTimePoint() const {
    return animation_->getEndOfFrameTimePoint();
}

std::string RepeatedAnimation::getName() const {
    return "repeated_" + std::to_string(repeatCount_) + "_" + animation_->getName();
}

void RepeatedAnimation::resetAnimation() {
    animation_->resetAnimation();
    repeatCounter_ = 0;
}

void RepeatedAnimation::updateTime(Animation::TimePoint timePoint) {
    animation_->updateTime(timePoint);

    if (animation_->finished() && (infinite_ || repeatCounter_ < repeatCount_)) {
        restartAnimation();
        ++repeatCounter_;
    }
}

void RepeatedAnimation::restartAnimation() {
    auto restartTime = animation_->getEndOfFrameTimePoint();
    animation_->resetAnimation();
    animation_->startAnimationFrom(restartTime);
}

void RepeatedAnimation::startAnimationFrom(Animation::TimePoint timePoint) {
    animation_->startAnimationFrom(timePoint);
}

std::chrono::nanoseconds RepeatedAnimation::getLength() const {
    return animation_->getLength();
}
