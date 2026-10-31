#pragma once

#include <chrono>
#include <deque>

class AmbientLightEstimate {
public:
    void updateLux(float lux);

    float getLux() const;

private:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::milliseconds;

    float calculateAverageLux(TimePoint time, Duration horizon);

    void setLux(float lux);

    std::pair<TimePoint, TimePoint> getTransitionTimes(TimePoint time);

private:
    std::deque<std::pair<TimePoint, float>> lightBuffer_;

    float currentLux_ = 0.0f;
    float brighteningThreshold_ = 0.0f;
    float darkeningThreshold_ = 0.0f;
};
