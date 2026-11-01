#pragma once

#include "timer_frontal_animation.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_composition.h>

class TimerAnimationComposition: public AnimationComposition {
public:
    TimerAnimationComposition(const std::string& aliceGifLedTemplates,
                              const std::string& fontBasePath,
                              std::shared_ptr<quasar::LedManager> ledManager,
                              std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                              std::chrono::system_clock::time_point firstTimerDueTime);

private:
    std::shared_ptr<TimerFrontalAnimation> timerFrontalAnimation_;
};
