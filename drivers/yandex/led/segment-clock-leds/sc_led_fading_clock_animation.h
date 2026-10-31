#pragma once

#include "sc_led_clock_animation.h"

class ScLedFadingClockAnimation: public ScLedClockAnimation {
public:
    ScLedFadingClockAnimation(std::weak_ptr<ScLedController> clockLedController,
                              int64_t utcOffsetSeconds,
                              bool withDegree = false);

    void drawCurrentFrame() override;

    bool finished() const override;

protected:
    bool finished_{false};
};
