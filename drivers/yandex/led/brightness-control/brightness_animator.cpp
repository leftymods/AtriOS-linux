#include "brightness_animator.h"

#include <yandex_io/libs/logging/logging.h>

#include <cmath>

namespace {
    constexpr uint32_t RATE = 60;
    constexpr auto DELAY = std::chrono::milliseconds(40);
} // namespace

BrightnessAnimator::BrightnessAnimator(std::function<void(uint32_t)> callback, uint32_t initialValue)
    : callback_(std::move(callback))
    , currentValue_(initialValue)
    , targetValue_(initialValue)
    , rate_(RATE)
    , delay_(DELAY)
{
    animatorThread_ = std::thread(std::bind(&BrightnessAnimator::animatorLoop, this));
}

BrightnessAnimator::~BrightnessAnimator() {
    stopped_ = true;
    cv_.notify_one();
    animatorThread_.join();
}

void BrightnessAnimator::animate(uint32_t target, bool smooth, uint32_t rate, std::chrono::milliseconds delay) {
    std::scoped_lock guard(mutex_);
    targetValue_ = target;
    if (smooth && targetValue_ != currentValue_) {
        YIO_LOG_DEBUG("Animating from " << currentValue_ << " to " << targetValue_);
        animatedValue_ = currentValue_;
        lastTime_ = std::chrono::steady_clock::now();
        rate_ = rate;
        delay_ = delay;
        cv_.notify_one();
    } else {
        currentValue_ = targetValue_;
        callback_(currentValue_);
    }
}

void BrightnessAnimator::animatorLoop() noexcept {
    while (!stopped_) {
        std::unique_lock lock(mutex_);

        const auto now = std::chrono::steady_clock::now();

        if (currentValue_ != targetValue_) {
            float timeDelta = std::chrono::duration<float>(now - lastTime_).count();
            lastTime_ = now;

            float delta = timeDelta * rate_;
            if (targetValue_ > currentValue_) {
                animatedValue_ = std::min<float>(animatedValue_ + delta, targetValue_);
            } else {
                animatedValue_ = std::max<float>(animatedValue_ - delta, targetValue_);
            }

            currentValue_ = std::round(animatedValue_);
            callback_(currentValue_);
        }

        if (currentValue_ != targetValue_) {
            cv_.wait_until(lock, now + delay_, [this]() {
                return stopped_.load();
            });
        } else {
            cv_.wait(lock, [this]() {
                return stopped_ || currentValue_ != targetValue_;
            });
        }
    }
}
