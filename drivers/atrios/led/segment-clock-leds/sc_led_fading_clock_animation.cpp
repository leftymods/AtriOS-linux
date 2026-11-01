#include "sc_led_fading_clock_animation.h"

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("leds");

ScLedFadingClockAnimation::ScLedFadingClockAnimation(std::weak_ptr<ScLedController> clockLedController,
                                                     int64_t utcOffsetSeconds,
                                                     bool withDegree)
    : ScLedClockAnimation(clockLedController, utcOffsetSeconds, withDegree)
{
    brightnessInd_ = brightnessCurve_.size() - 1;
}

bool ScLedFadingClockAnimation::finished() const {
    return finished_;
}

void ScLedFadingClockAnimation::drawCurrentFrame()
{
    if (!started() || finished()) {
        return;
    }

    ScLedFrame frame;
    try {
        std::string degreeStr = withDegree_ ? "*" : " ";
        frame = ScLedFrame::makeFrameFromString(timeStr_ + degreeStr, brightnessCurve_[brightnessInd_]);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("ScLedFadingClockAnimation.DrawCurrentFrame.Exception", "Exception while drawing fadingClockAnimation: " << e.what());
        return;
    }

    if (auto clockLedController = clockLedController_.lock()) {
        clockLedController->drawFrame(std::move(frame));
    }

    if (brightnessInd_ == 0) {
        finished_ = true;
    }

    --brightnessInd_;
}
