#include "sc_led_timer_countdown_animation.h"

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("leds");

namespace {
    constexpr std::chrono::milliseconds FRAME_LEN_WHILE_LIGHT_UP = std::chrono::milliseconds(10);
    constexpr std::chrono::milliseconds FRAME_LEN = std::chrono::milliseconds(250);
} // namespace

const std::vector<uint8_t> ScLedTimerCountdownAnimation::brightnessCurve_ = {0, 0, 16, 32, 48, 64, 80, 96, 128, 160, 192, 224, 255};

ScLedTimerCountdownAnimation::ScLedTimerCountdownAnimation(std::weak_ptr<ScLedController> clockLedController,
                                                           std::chrono::system_clock::time_point timerEnd,
                                                           bool withDegree,
                                                           bool withSmoothLightUp)
    : TimerCountdownAnimationBase(clockLedController, timerEnd)
    , clockLedController_(std::move(clockLedController))
    , withDegree_(withDegree)
{
    frameLength_ = FRAME_LEN_WHILE_LIGHT_UP;
    if (!withSmoothLightUp) {
        brightnessInd_ = brightnessCurve_.size() - 1;
        frameLength_ = FRAME_LEN;
    }
}

void ScLedTimerCountdownAnimation::drawCurrentFrame()
{
    if (!started()) {
        return;
    }

    if (brightnessInd_ != brightnessCurve_.size() - 1) {
        ++brightnessInd_;
        if (brightnessInd_ == brightnessCurve_.size() - 1) {
            frameLength_ = FRAME_LEN;
        }
    }

    ScLedFrame frame;
    try {
        std::string degreeStr = withDegree_ ? "*" : " ";
        frame = ScLedFrame::makeFrameFromString(timeStr_ + degreeStr, brightnessCurve_[brightnessInd_]);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("ScLedTimerCountdownAnimation.DrawCurrentFrame.Exception", "Exception while drawing timer  countdown animation: " << e.what());
        return;
    }

    if (auto clockLedController = clockLedController_.lock()) {
        clockLedController->drawFrame(std::move(frame));
    }
}
