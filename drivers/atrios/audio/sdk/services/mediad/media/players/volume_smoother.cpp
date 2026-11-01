#include "volume_smoother.h"

#include <algorithm>
#include <mutex>

using namespace quasar;

VolumeSmoother::VolumeSmoother(float currentVolume)
    : VolumeSmoother(currentVolume, [] { return std::chrono::steady_clock::now(); })
{
}

VolumeSmoother::VolumeSmoother(float currentVolume, std::function<std::chrono::steady_clock::time_point()> clock)
    : clock_(std::move(clock))
    , startVolume_(std::clamp(currentVolume, 0.f, 1.f))
    , targetVolume_(startVolume_)
    , startTime_(clock_())
    , targetTime_(clock_())
{
}

void VolumeSmoother::setVolume(float targetVolume, std::chrono::milliseconds period)
{
    std::unique_lock<std::mutex> lock(mutex_);

    auto now = clock_();
    auto startVolume = currentVolumeUnsafe(now);
    auto absDiff = std::abs(targetVolume - startVolume);

    period = std::chrono::milliseconds{std::max<int>(period.count() * absDiff, 0)};
    targetVolume = std::clamp(targetVolume, 0.f, 1.f);

    startVolume_ = startVolume;
    targetVolume_ = targetVolume;
    startTime_ = now;
    targetTime_ = startTime_ + period;
}

float VolumeSmoother::currentVolume() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return currentVolumeUnsafe(clock_());
}

float VolumeSmoother::targetVolume() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return targetVolume_;
}

std::chrono::milliseconds VolumeSmoother::duration() const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto now = clock_();
    if (now >= targetTime_ || startTime_ >= targetTime_) {
        return std::chrono::milliseconds{0};
    }
    return std::max(std::chrono::duration_cast<std::chrono::milliseconds>(targetTime_ - now), std::chrono::milliseconds{1});
}

float VolumeSmoother::currentVolumeUnsafe(std::chrono::steady_clock::time_point now) const {
    if (now >= targetTime_ || startTime_ >= targetTime_) {
        return targetVolume_;
    }

    auto diff = targetVolume_ - startVolume_;
    auto passed = static_cast<double>(now.time_since_epoch().count() - startTime_.time_since_epoch().count());
    auto period = static_cast<double>(targetTime_.time_since_epoch().count() - startTime_.time_since_epoch().count());
    auto nowVolume = startVolume_ + diff * passed / period;
    if (targetVolume_ > startVolume_) {
        return std::clamp<float>(nowVolume, 0.f, targetVolume_);
    } else {
        return std::clamp<float>(nowVolume, targetVolume_, 1.f);
    }
}
