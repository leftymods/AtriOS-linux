#include "speaking_start_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

SpeakingStartAnimationComposition::SpeakingStartAnimationComposition(
    const std::string& aliceGifLedAnimations,
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
    const std::shared_ptr<quasar::LedManager>& ledManager [[maybe_unused]])
    : AnimationComposition(std::vector<std::shared_ptr<Animation>>{
                               GifAnimation::readGif(aliceGifLedAnimations + "oknix/01-activation.gif",
                                                     yandexStationFrontalController),
                               ledManager->getPattern("speaking.led")}, false)
{
}
