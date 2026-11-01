#pragma once

#include "timer_animation_composition.h"

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>

class TimerConductor: public AnimationConductor {
public:
    TimerConductor(const std::string& aliceGifLedTemplates,
                   const std::string& fontBasePath,
                   std::shared_ptr<quasar::LedManager> ledManager,
                   std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                   std::chrono::system_clock::time_point firstTimerDueTime);

    std::string getName() const override;

private:
    std::shared_ptr<TimerAnimationComposition> TimerAnimationComposition_;
};
