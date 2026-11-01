#pragma once

#include <yandex_io/modules/leds/led_manager/ng/animation.h>

#include <memory>

/**
 * Two or more "concatenated" single-device animations
 */
class CompoundAnimation: public Animation {
public:
    static constexpr int INFINITE = 1;

public:
    CompoundAnimation(std::vector<std::shared_ptr<Animation>> animations, std::weak_ptr<quasar::LedDevice> ledDevice);

    ~CompoundAnimation() override = default;

    void drawCurrentFrame() override;

    bool finished() const override;

    TimePoint getEndOfFrameTimePoint() const override;

    std::string getName() const override;

    void resetAnimation() override;
    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    std::chrono::nanoseconds getLength() const override;

private:
    std::vector<std::shared_ptr<Animation>> animations_;
    std::vector<std::shared_ptr<Animation>>::iterator currentAnimationIterator_;
    std::vector<std::chrono::nanoseconds> animationLengths_;
    bool started_ = false;
};
