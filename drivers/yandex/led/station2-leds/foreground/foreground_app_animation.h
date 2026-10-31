#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/foreground/app/base/foreground_app.h>

#include <yandex_io/modules/leds/led_manager/ng/animation.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/draw_utils.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>

#include <memory>

class ForegroundAppAnimation: public Animation {
public:
    ForegroundAppAnimation(
        std::weak_ptr<YandexStationFrontalController> ledDevice,
        std::shared_ptr<ForegroundApp> app);

    void drawCurrentFrame() override;

    [[nodiscard]] bool finished() const override;

    [[nodiscard]] TimePoint getEndOfFrameTimePoint() const override;

    [[nodiscard]] std::string getName() const override;

    void resetAnimation() override;
    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    std::chrono::nanoseconds getLength() const override;

private:
    std::weak_ptr<YandexStationFrontalController> ledDevice_;
    std::shared_ptr<ForegroundApp> app_;
    quasar::Screen screen_;
    std::chrono::steady_clock::time_point endOfFrame_;
};
