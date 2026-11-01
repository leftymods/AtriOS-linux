#include "compound_animation.h"

#include <yandex_io/libs/logging/logging.h>

#include <utility>

CompoundAnimation::CompoundAnimation(std::vector<std::shared_ptr<Animation>> animations, std::weak_ptr<quasar::LedDevice> ledDevice)
    : Animation(std::move(ledDevice))
    , animations_(std::move(animations))
{
    currentAnimationIterator_ = animations_.begin();
    animationLengths_.reserve(animations_.size());
    std::transform(animations_.begin(), animations_.end(), std::back_inserter(animationLengths_),
                   [](const std::shared_ptr<Animation>& animation) -> std::chrono::nanoseconds {
                       return animation->getLength();
                   });
}

void CompoundAnimation::drawCurrentFrame() {
    currentAnimationIterator_->get()->drawCurrentFrame();
}

bool CompoundAnimation::finished() const {
    if (animations_.empty()) {
        return true;
    }
    return currentAnimationIterator_ == animations_.end();
}

Animation::TimePoint CompoundAnimation::getEndOfFrameTimePoint() const {
    if (finished()) {
        return std::chrono::steady_clock::now();
    }
    return currentAnimationIterator_->get()->getEndOfFrameTimePoint();
}

std::string CompoundAnimation::getName() const {
    return "<CompoundAnimation>";
}

void CompoundAnimation::resetAnimation() {
    for (const auto& animation : animations_) {
        animation->resetAnimation();
    }
    currentAnimationIterator_ = animations_.begin();
}

void CompoundAnimation::updateTime(Animation::TimePoint timePoint) {
    std::for_each(currentAnimationIterator_, animations_.end(),
                  [&timePoint](const std::shared_ptr<Animation>& animation) { animation->updateTime(timePoint); });

    currentAnimationIterator_ = std::find_if(currentAnimationIterator_, animations_.end(), [](auto& value) {
        return !value->finished();
    });
}

void CompoundAnimation::startAnimationFrom(Animation::TimePoint timePoint) {
    if (started_) {
        return;
    }
    started_ = true;
    auto length = std::chrono::nanoseconds(0);
    for (unsigned long i = 0; i < animations_.size(); ++i) {
        const auto& animation = animations_[i];
        animation->startAnimationFrom(timePoint + length);
        length += animationLengths_[i];
    }
}

std::chrono::nanoseconds CompoundAnimation::getLength() const {
    auto length = std::chrono::nanoseconds(0);
    for (const auto& animation : animations_) {
        length += animation->getLength();
    }
    return length;
}
