#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_composition.h>

class SpeakingStartAnimationComposition: public AnimationComposition {
public:
    SpeakingStartAnimationComposition(const std::string& aliceGifLedAnimations,
                                      std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                                      const std::shared_ptr<quasar::LedManager>& ledManager);
};
