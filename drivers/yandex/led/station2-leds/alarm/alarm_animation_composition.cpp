#include "alarm_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/repeated_animation.h>

AlarmAnimationComposition::AlarmAnimationComposition(std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController, const std::shared_ptr<quasar::LedManager>& ledManager, const std::string& aliceGifLedAnimations)
    : AnimationComposition(
          std::vector<std::shared_ptr<Animation>>{
              std::make_shared<RepeatedAnimation>(
                  GifAnimation::readGif(aliceGifLedAnimations + "alarm/25x16_alarm_loop.gif",
                                        yandexStationFrontalController),
                  6),
              ledManager->getPattern("timer.led")},
          true)
{
}
