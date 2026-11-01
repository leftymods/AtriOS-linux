#pragma once

#include <yandex_io/modules/leds/led_manager/ng/animation.h>

#include <memory>

class RepeatedAnimation: public Animation {
public:
    static constexpr int INFINITE = 1;

public:
    explicit RepeatedAnimation(std::shared_ptr<Animation> animation, int repeatCount = 1);

    ~RepeatedAnimation() override = default;

    void drawCurrentFrame() override;

    bool finished() const override;

    TimePoint getEndOfFrameTimePoint() const override;

    std::string getName() const override;

    void resetAnimation() override;
    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    [[nodiscard]] std::chrono::nanoseconds getLength() const override;

private:
    std::shared_ptr<Animation> animation_;
    bool infinite_ = false;
    uint64_t repeatCount_;
    uint64_t repeatCounter_ = 0;
    void restartAnimation();
};
