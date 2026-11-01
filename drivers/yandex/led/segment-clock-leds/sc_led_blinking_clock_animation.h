#pragma once

#include "sc_led_clock_animation.h"

#include <yandex_io/modules/leds/led_manager/ng/clock_animation_base.h>

#include <vector>

class ScLedBlinkingClockAnimation: public ScLedClockAnimation {
public:
    ScLedBlinkingClockAnimation(std::weak_ptr<ScLedController> clockLedController,
                                int64_t utcOffsetSeconds,
                                bool withDegree = false);

    void drawCurrentFrame() override;

protected:
    int inc_{1};
};
