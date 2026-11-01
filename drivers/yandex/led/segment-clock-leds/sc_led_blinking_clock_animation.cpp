#include "sc_led_blinking_clock_animation.h"
#include "sc_led_frame.h"

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("leds");

namespace {
    constexpr std::chrono::milliseconds FRAME_LEN_CHANGING = std::chrono::milliseconds(20);
    constexpr std::chrono::milliseconds FRAME_LEN_HOLD = std::chrono::milliseconds(250);
} // namespace

ScLedBlinkingClockAnimation::ScLedBlinkingClockAnimation(std::weak_ptr<ScLedController> clockLedController,
                                                         int64_t utcOffsetSeconds,
                                                         bool withDegree)
    : ScLedClockAnimation(clockLedController, utcOffsetSeconds, withDegree)
{
    frameLength_ = FRAME_LEN_CHANGING;
}

void ScLedBlinkingClockAnimation::drawCurrentFrame()
{
    if (!started()) {
        return;
    }

    if (brightnessInd_ == 0 || brightnessInd_ == brightnessCurve_.size() - 1) {
        frameLength_ = FRAME_LEN_CHANGING;
    }

    brightnessInd_ += inc_;
    if (brightnessInd_ == 0) {
        inc_ = 1;
        frameLength_ = FRAME_LEN_HOLD;
    } else if (brightnessInd_ == brightnessCurve_.size() - 1) {
        inc_ = -1;
        frameLength_ = FRAME_LEN_HOLD;
    }

    ScLedFrame frame;
    try {
        std::string degreeStr = withDegree_ ? "*" : " ";
        frame = ScLedFrame::makeFrameFromString(timeStr_ + degreeStr, ScLedClockAnimation::brightnessCurve_[brightnessInd_]);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("ScLedBlinkingClockAnimation.DrawCurrentFrame.Exception", "Exception while drawing clockAnimation: " << e.what());
        return;
    }

    if (auto clockLedController = clockLedController_.lock()) {
        clockLedController->drawFrame(std::move(frame));
    }
}
