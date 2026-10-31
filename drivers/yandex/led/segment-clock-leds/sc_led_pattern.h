#pragma once

#include <yandex_io/modules/leds/led_manager/ng/animation.h>

#include "sc_led_controller.h"
#include "sc_led_frame.h"

class ScLedPattern: public Animation {
public:
    ScLedPattern(std::string name, std::weak_ptr<ScLedController> ledController);
    ScLedPattern(const ScLedPattern& other) = default;

    static std::shared_ptr<ScLedPattern> loadFromFile(const std::string& fileName, std::weak_ptr<ScLedController> ledControllerWeak);
    static std::shared_ptr<ScLedPattern> loadFromBinary(const std::string& name,
                                                        const std::vector<uint8_t>& bytes,
                                                        std::weak_ptr<ScLedController> ledControllerWeak);

    static std::shared_ptr<ScLedPattern> getIdlePattern(std::weak_ptr<ScLedController> ledController);

    bool finished() const override;
    void drawCurrentFrame() override;
    TimePoint getEndOfFrameTimePoint() const override;
    void resetAnimation() override;
    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint startTime) override;
    std::chrono::nanoseconds getLength() const override;

    std::string getName() const override;

private:
    const ScLedFrame& getCurrentFrame() const;
    void load(std::istream& stream);

protected:
    const std::string name_;
    int loopFromIndex_ = -1;
    std::vector<ScLedFrame> frames_;

private:
    std::weak_ptr<ScLedController> ledDevice_;

    bool started_ = false;
    int currentFrame_ = 0;
    TimePoint startTime_;
    TimePoint endOfFrame_;
    TimePoint endOfDuration_ = std::chrono::steady_clock::time_point::max();
    TimePoint startOfCurrentFrame_;
};
