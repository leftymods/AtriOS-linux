#pragma once

#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <thread>

class BrightnessAnimator {
public:
    BrightnessAnimator(std::function<void(uint32_t)> callback, uint32_t initialValue = 0);
    ~BrightnessAnimator();

    void animate(uint32_t target, bool smooth = true, uint32_t rate = 60, std::chrono::milliseconds delay = std::chrono::milliseconds{40});

private:
    void animatorLoop() noexcept;

private:
    std::function<void(uint32_t)> callback_;

    std::mutex mutex_;
    quasar::SteadyConditionVariable cv_;

    std::thread animatorThread_;
    std::atomic<bool> stopped_ = false;

    uint32_t currentValue_ = 0;
    uint32_t targetValue_ = 0;

    uint32_t rate_;
    std::chrono::milliseconds delay_;

    float animatedValue_;
    std::chrono::steady_clock::time_point lastTime_;
};
