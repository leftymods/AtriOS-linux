#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

#include <queue>

enum class TimerFrameType {
    EARLY = 0,
    EXIT = 1,
    COUNTDOWN = 2,
    BELL = 3
};

struct TimerFrame {
    TimerFrameType type;
    std::optional<std::array<char, 4>> timeLeft;
    std::optional<std::array<char, 4>> nextTimeLeft;
    quasar::ReplaceList getReplace();
};

class TimerFrameHelper {
private:
    std::chrono::system_clock::time_point firstTimerDueTime_;

public:
    // fuchsia, green, red, blue
    static const std::vector<BGR24> colors_;

    explicit TimerFrameHelper(std::chrono::system_clock::time_point firstTimerDueTime);

    TimerFrame getFrame(std::chrono::system_clock::time_point time) const;

    static std::optional<std::array<char, 4>> toClockSymbols(std::chrono::milliseconds timeLeft);
};

class TimerFrontalAnimation: public GifAnimation {
public:
    TimerFrontalAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice,
                          const std::string& aliceGifLedTemplates,
                          const std::string& fontBasePath,
                          std::chrono::system_clock::time_point firstTimerDueTime);
    std::string getName() const override;
    TimePoint getEndOfFrameTimePoint() const override;

private:
    TimerFrameHelper timerFrameHelper_;

    bool finished() const override;

    FrameBufferOutputFrame getCurrentFrame() const override;

    FrameBufferOutputFrame renderFrame(TimerFrame frame) const;

    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    std::chrono::nanoseconds getLength() const override;

    quasar::Font font_;

    quasar::Gif gif_;
};
