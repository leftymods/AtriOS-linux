#include "sc_led_clock_animation.h"

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("leds");

namespace {
    constexpr std::chrono::milliseconds EMPTY_SCREEN_FRAME_LEN = std::chrono::milliseconds(500);
    constexpr std::chrono::milliseconds FRAME_LEN_WHILE_LIGHT_UP = std::chrono::milliseconds(10);
    constexpr std::chrono::milliseconds FRAME_LEN = std::chrono::milliseconds(1000);
} // namespace

const std::vector<uint8_t> ScLedClockAnimation::brightnessCurve_ = {0, 0, 16, 32, 48, 64, 80, 96, 128, 160, 192, 224, 255};

ScLedClockAnimation::ScLedClockAnimation(std::weak_ptr<ScLedController> clockLedController,
                                         int64_t utcOffsetSeconds,
                                         bool withDegree,
                                         bool withSmoothLightUp)
    : ClockAnimationBase(clockLedController, utcOffsetSeconds)
    , clockLedController_(std::move(clockLedController))
    , withDegree_(withDegree)
{
    frameLength_ = EMPTY_SCREEN_FRAME_LEN;
    if (!withSmoothLightUp) {
        brightnessInd_ = brightnessCurve_.size() - 1;
        frameLength_ = FRAME_LEN;
    }
}

void ScLedClockAnimation::drawCurrentFrame() {
    if (!started()) {
        return;
    }

    if (brightnessInd_ != brightnessCurve_.size() - 1) {
        ++brightnessInd_;
        if (brightnessInd_ == 1) {
            frameLength_ = FRAME_LEN_WHILE_LIGHT_UP;
        } else if (brightnessInd_ == brightnessCurve_.size() - 1) {
            frameLength_ = FRAME_LEN;
        }
    }

    ScLedFrame frame;
    try {
        std::string degreeStr = withDegree_ ? "*" : " ";
        frame = ScLedFrame::makeFrameFromString(timeStr_ + degreeStr, brightnessCurve_[brightnessInd_]);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("ScLedClockAnimation.DrawCurrentFrame.Exception", "Exception while drawing clockAnimation: " << e.what());
        return;
    }

    if (auto clockLedController = clockLedController_.lock()) {
        clockLedController->drawFrame(std::move(frame));
    }
}
