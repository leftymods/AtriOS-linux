#include "ambient_light_estimate.h"

#include <yandex_io/libs/logging/logging.h>

namespace {

    constexpr auto LIGHT_HORIZON_LONG = std::chrono::milliseconds(10000);
    constexpr auto LIGHT_HORIZON_SHORT = std::chrono::milliseconds(2000);

    constexpr float BRIGHTENING_THRESHOLD = 0.1f;
    constexpr float DARKENING_THRESHOLD = 0.1f;

    constexpr auto BRIGHTENING_DELAY = std::chrono::milliseconds(5000);
    constexpr auto DARKENING_DELAY = std::chrono::milliseconds(5000);

    float calculateSampleWeight(std::chrono::milliseconds start, std::chrono::milliseconds end) {
        auto w = [](int64_t x) {
            return x * (x * 0.5f + LIGHT_HORIZON_LONG.count());
        };

        return w(end.count()) - w(start.count());
    }

} // namespace

void AmbientLightEstimate::updateLux(float lux) {
    const TimePoint now = std::chrono::steady_clock::now();
    const bool firstUpdate = lightBuffer_.empty();

    while (!lightBuffer_.empty() && lightBuffer_.front().first < now - LIGHT_HORIZON_LONG) {
        lightBuffer_.pop_front();
    }
    lightBuffer_.emplace_back(now, lux);

    float averageLuxSlow = calculateAverageLux(now, LIGHT_HORIZON_LONG);
    float averageLuxFast = calculateAverageLux(now, LIGHT_HORIZON_SHORT);
    const auto [nextBrighteningTime, nextDarkeningTime] = getTransitionTimes(now);

    if ((averageLuxSlow >= brighteningThreshold_ && averageLuxFast >= brighteningThreshold_ && nextBrighteningTime <= now) || (averageLuxSlow <= darkeningThreshold_ && averageLuxFast <= darkeningThreshold_ && nextDarkeningTime <= now) || firstUpdate) {
        setLux(averageLuxFast);
    }
}

float AmbientLightEstimate::getLux() const {
    return currentLux_;
}

float AmbientLightEstimate::calculateAverageLux(TimePoint time, Duration horizon) {
    if (lightBuffer_.empty()) {
        return 0.0f;
    }

    float sum = 0.0f;
    float totalWeight = 0.0f;

    auto endTime = std::chrono::milliseconds(100);

    for (auto it = lightBuffer_.rbegin(); it != lightBuffer_.rend(); it++) {
        if (it->first < time - horizon) {
            break;
        }

        auto startTime = std::chrono::duration_cast<std::chrono::milliseconds>(it->first - time);
        float weight = calculateSampleWeight(startTime, endTime);

        sum += it->second * weight;
        totalWeight += weight;

        endTime = startTime;
    }

    return sum / totalWeight;
}

void AmbientLightEstimate::setLux(float lux) {
    currentLux_ = lux;
    brighteningThreshold_ = currentLux_ * (1.0f + BRIGHTENING_THRESHOLD);
    darkeningThreshold_ = currentLux_ * (1.0f - DARKENING_THRESHOLD);

    YIO_LOG_DEBUG("lux " << currentLux_
                         << " brighteningThreshold " << brighteningThreshold_
                         << " darkeningLuxThreshold " << darkeningThreshold_);
}

std::pair<AmbientLightEstimate::TimePoint, AmbientLightEstimate::TimePoint> AmbientLightEstimate::getTransitionTimes(AmbientLightEstimate::TimePoint time) {
    TimePoint brighteningTime = time;
    TimePoint darkeningTime = time;

    bool brighteningTimeFound = false;
    bool darkeningTimeFound = false;

    for (auto it = lightBuffer_.rbegin(); it != lightBuffer_.rend(); it++) {
        if (!brighteningTimeFound) {
            if (it->second <= brighteningThreshold_) {
                brighteningTimeFound = true;
            } else {
                brighteningTime = it->first;
            }
        }

        if (!darkeningTimeFound) {
            if (it->second >= darkeningThreshold_) {
                darkeningTimeFound = true;
            } else {
                darkeningTime = it->first;
            }
        }
    }

    return {brighteningTime + BRIGHTENING_DELAY, darkeningTime + DARKENING_DELAY};
}
