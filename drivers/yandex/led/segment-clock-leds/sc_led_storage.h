#pragma once

#include "sc_led_blinking_clock_animation.h"
#include "sc_led_clock_animation.h"
#include "sc_led_controller.h"
#include "sc_led_fading_clock_animation.h"
#include "sc_led_timer_countdown_animation.h"
#include "sc_led_pattern.h"

#include <string>
#include <unordered_map>

class ScLedStorage {
public:
    ScLedStorage(std::weak_ptr<ScLedController> ledController, const std::string& ledPatternsPath);

    std::shared_ptr<ScLedPattern> getPattern(const std::string& name) const;
    std::shared_ptr<ScLedPattern> getIdlePattern() const;
    std::shared_ptr<ScLedPattern> getVolumePattern(int currentVolume, int maxVolume) const;

    std::shared_ptr<ScLedClockAnimation> getClockAnimation(int64_t utcOffsetSeconds, bool withDegree, bool withSmoothLightUp) const;
    std::shared_ptr<ScLedFadingClockAnimation> getFadingClockAnimation(int64_t utcOffsetSeconds, bool withDegree) const;
    std::shared_ptr<ScLedBlinkingClockAnimation> getBlinkingClockAnimation(int64_t utcOffsetSeconds, bool withDegree) const;
    std::shared_ptr<ScLedTimerCountdownAnimation> getCountdownAnimation(std::chrono::system_clock::time_point timerEnd, bool withDegree, bool withSmoothLightUp) const;

private:
    void loadPatterns(const std::string& pathToPatternsDirectory);

private:
    std::weak_ptr<ScLedController> ledController_;

    std::unordered_map<std::string, std::shared_ptr<ScLedPattern>> patterns_;
};
