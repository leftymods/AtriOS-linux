#include "alarm_animation_conductor.h"
#include "alarm_animation_composition.h"

#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>

#include <memory>
#include <vector>

AlarmAnimationConductor::AlarmAnimationConductor(std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController, const std::shared_ptr<quasar::LedManager>& ledManager, const std::string& aliceGifLedAnimations)
    : AnimationConductor(
          std::vector<const std::shared_ptr<AnimationComposition>>{
              std::make_shared<AlarmAnimationComposition>(
                  yandexStationFrontalController,
                  ledManager,
                  aliceGifLedAnimations)})
{
}

std::string AlarmAnimationConductor::getName() const {
    return " <AlarmAnimationConductor> ";
}
