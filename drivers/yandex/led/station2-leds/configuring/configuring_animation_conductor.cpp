#include "configuring_animation_conductor.h"
#include "configuring_animation_composition.h"
#include "configuring_long_animation_composition.h"

#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>

#include <memory>
#include <vector>

ConfiguringAnimationConductor::ConfiguringAnimationConductor(const std::string& aliceGifLedAnimations,
                                                             std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                                                             const std::shared_ptr<quasar::LedManager>& ledManager,
                                                             std::shared_ptr<Animation> configurePattern)
    : AnimationConductor(
          std::vector<const std::shared_ptr<AnimationComposition>>{
              std::make_shared<ConfiguringAnimationComposition>(aliceGifLedAnimations,
                                                                yandexStationFrontalController,
                                                                ledManager,
                                                                configurePattern),
              std::make_shared<ConfiguringLongAnimationComposition>(aliceGifLedAnimations,
                                                                    std::move(yandexStationFrontalController),
                                                                    ledManager,
                                                                    configurePattern),

          })
{
}

std::string ConfiguringAnimationConductor::getName() const {
    return " <ConfiguringAnimationConductor> ";
}
