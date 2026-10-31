#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/draw_utils.h>

#include <yandex_io/modules/leds/led_manager/ng/clock_animation_base.h>

#include <memory>
#include <string>

class ClockAnimation: public ClockAnimationBase {
public:
    ClockAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice,
                   FontConfig fontConfig);

    void drawCurrentFrame() override;

    void setFontConfig(FontConfig config);

private:
    std::weak_ptr<YandexStationFrontalController> ledDevice_;
    quasar::Screen screen_;

    std::mutex mutex_;
    FontConfig fontConfig_;
    quasar::Font font_;
};
