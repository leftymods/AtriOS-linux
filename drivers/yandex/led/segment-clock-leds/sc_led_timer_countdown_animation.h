#pragma once

#include <smart_devices/libs/segment_clock_leds/sc_led_controller.h>

#include <yandex_io/modules/leds/led_manager/ng/timer_countdown_animation_base.h>

#include <memory>

class ScLedTimerCountdownAnimation: public TimerCountdownAnimationBase {
public:
    ScLedTimerCountdownAnimation(std::weak_ptr<ScLedController> clockLedController,
                                 std::chrono::system_clock::time_point timerEnd,
                                 bool withDegree,
                                 bool withSmoothLightUp);

    void drawCurrentFrame() override;

private:
    std::weak_ptr<ScLedController> clockLedController_;
    const bool withDegree_;

    static const std::vector<uint8_t> brightnessCurve_;
    size_t brightnessInd_{0};
};
