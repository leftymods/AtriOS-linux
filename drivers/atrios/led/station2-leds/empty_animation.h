#pragma once

#include "frontal_animation/yandex_station_frontal_controller.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/draw_utils.h>

#include <yandex_io/modules/leds/led_manager/ng/animation.h>

#include <chrono>
#include <optional>

class EmptyAnimation: public Animation {
public:
    explicit EmptyAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice);

    ~EmptyAnimation() override = default;

    void drawCurrentFrame() override;

    bool finished() const override;

    TimePoint getEndOfFrameTimePoint() const override;

    std::string getName() const override;

    void resetAnimation() override;

    /* provide a timepoint when animation should "end" */
    void setFinishedTimepoint(std::chrono::steady_clock::time_point tp);
    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    std::chrono::nanoseconds getLength() const override;

private:
    std::weak_ptr<YandexStationFrontalController> ledDevice_;

    const quasar::Screen screen_;
    std::optional<std::chrono::steady_clock::time_point> endOfAnimationTp_;

    std::chrono::time_point<std::chrono::steady_clock> endOfFrame_;
    TimePoint currentTime_;
};
