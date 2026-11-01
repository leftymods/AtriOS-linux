#pragma once

#include <smart_devices/libs/segment_clock_leds/sc_led_controller.h>

#include <yandex_io/modules/leds/led_manager/ng/clock_animation_base.h>

#include <vector>

class ScLedClockAnimation: public ClockAnimationBase {
public:
    ScLedClockAnimation(std::weak_ptr<ScLedController> clockLedController,
                        int64_t utcOffsetSeconds,
                        bool withDegree = false,
                        bool withSmoothLightUp = true);

    void drawCurrentFrame() override;

protected:
    std::weak_ptr<ScLedController> clockLedController_;
    const bool withDegree_;

    static const std::vector<uint8_t> brightnessCurve_;
    size_t brightnessInd_{0};
};
