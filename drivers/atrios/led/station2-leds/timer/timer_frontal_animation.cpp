
#include "timer_frontal_animation.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

namespace {

    constexpr double timerAnimationCustomGamma = 1.8;
    constexpr std::chrono::milliseconds frameDuration = std::chrono::milliseconds(250);
    constexpr std::chrono::milliseconds animationShift = std::chrono::milliseconds(300);
    constexpr std::chrono::milliseconds hour = std::chrono::hours(1);
    constexpr std::chrono::milliseconds second = std::chrono::seconds(1);

} // namespace

const std::vector<BGR24> TimerFrameHelper::colors_ = std::vector<BGR24>{
    {255, 0, 255},
    {0, 255, 0},
    {0, 0, 255},
    {255, 0, 0}};

TimerFrontalAnimation::TimerFrontalAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice,
                                             const std::string& aliceGifLedTemplates,
                                             const std::string& fontBasePath,
                                             std::chrono::system_clock::time_point firstTimerDueTime)
    : GifAnimation(ledDevice)
    , timerFrameHelper_(firstTimerDueTime)
    , gif_(quasar::loadGif(aliceGifLedTemplates + "transform_still.gif"))
{
    auto fontSource = quasar::loadFont(fontBasePath + "clock_large.gif");
    if (auto sharedLedDevice = ledDevice.lock()) {
        for (auto& item : fontSource) {
            sharedLedDevice->getColorConfig().applyDeviceCorrection(item.second, timerAnimationCustomGamma);
        }
    }
    font_ = std::move(fontSource);
}

FrameBufferOutputFrame TimerFrontalAnimation::getCurrentFrame() const {
    return renderFrame(timerFrameHelper_.getFrame(std::chrono::system_clock::now()));
}

bool TimerFrontalAnimation::finished() const {
    return timerFrameHelper_.getFrame(std::chrono::system_clock::now()).type == TimerFrameType::BELL;
}

FrameBufferOutputFrame TimerFrontalAnimation::renderFrame(TimerFrame timerFrame) const {
    if (timerFrame.type == TimerFrameType::COUNTDOWN || timerFrame.type == TimerFrameType::BELL) {
        quasar::Gif gifCopy = gif_;
        const quasar::Gif loaded = quasar::renderFont(
            timerFrame.getReplace(), font_,
            std::move(gifCopy),
            YandexStationFrontalController::screenShape);
        return FrameBufferOutputFrame(loaded.begin()->data);
    } else {
        return FrameBufferOutputFrame();
    }
}

TimerFrameHelper::TimerFrameHelper(std::chrono::system_clock::time_point firstTimerDueTime)
    : firstTimerDueTime_(firstTimerDueTime)
{
}

std::chrono::milliseconds toTimeLeft(std::chrono::milliseconds timeAfterStart) {
    return hour - timeAfterStart;
}

std::optional<std::array<char, 4>> TimerFrameHelper::toClockSymbols(std::chrono::milliseconds timeLeft) {
    auto wholeMinutes = std::chrono::duration_cast<std::chrono::minutes>(timeLeft);
    auto secondsInMinute = std::chrono::duration_cast<std::chrono::seconds>(timeLeft - wholeMinutes);
    std::array<char, 4> result = {
        char('0' + wholeMinutes.count() / 10),
        char('0' + wholeMinutes.count() % 10),
        char('0' + secondsInMinute.count() / 10),
        char('0' + secondsInMinute.count() % 10)};
    return result;
}

TimerFrame TimerFrameHelper::getFrame(std::chrono::system_clock::time_point time) const {
    time = time - animationShift;
    if (time >= firstTimerDueTime_) {
        std::array<char, 4> zeros = {'0', '0', '0', '0'};
        return TimerFrame{TimerFrameType::BELL, std::make_optional(zeros), std::make_optional(zeros)};
    }

    if (time < firstTimerDueTime_ - hour) {
        return TimerFrame{TimerFrameType::EARLY, std::nullopt, std::nullopt};
    }

    std::chrono::milliseconds timeAfterStart = std::chrono::duration_cast<std::chrono::milliseconds>(time - firstTimerDueTime_ + hour);
    return TimerFrame{TimerFrameType::COUNTDOWN,
                      toClockSymbols(toTimeLeft(timeAfterStart)),
                      toClockSymbols(toTimeLeft(timeAfterStart + second))};
}

quasar::ReplaceList TimerFrame::getReplace() {
    quasar::ReplaceList result;
    if (!timeLeft || !nextTimeLeft) {
        return result;
    }
    for (int i = 0; i < 4; ++i) {
        char buf[3];
        buf[0] = timeLeft.value()[i];
        buf[1] = '_';
        buf[2] = nextTimeLeft.value()[i];

        result.emplace_back(std::string(buf, 3), TimerFrameHelper::colors_[i]);
    }
    return result;
}

void TimerFrontalAnimation::updateTime(Animation::TimePoint timePoint) {
    if (!started_) {
        return;
    }
    // simulate constant frame rate
    while (timePoint >= endOfFrame_) {
        endOfFrame_ = endOfFrame_ + frameDuration;
    }
}

void TimerFrontalAnimation::startAnimationFrom(Animation::TimePoint timePoint) {
    started_ = true;
    endOfFrame_ = timePoint + std::chrono::milliseconds(1);
}

std::chrono::nanoseconds TimerFrontalAnimation::getLength() const {
    return frameDuration;
}

std::string TimerFrontalAnimation::getName() const {
    return "<TimerFrontalAnimation>";
}

Animation::TimePoint TimerFrontalAnimation::getEndOfFrameTimePoint() const {
    return GifAnimation::getEndOfFrameTimePoint();
}
