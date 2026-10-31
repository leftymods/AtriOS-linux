#include "oknyx_out_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

OknyxOutAnimationComposition::OknyxOutAnimationComposition(
    const std::string& aliceGifLedAnimations,
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
    const std::shared_ptr<quasar::LedManager>& ledManager [[maybe_unused]])
    : AnimationComposition(std::vector<std::shared_ptr<Animation>>{
                               GifAnimation::readGif(aliceGifLedAnimations + "oknix/00-out.gif", yandexStationFrontalController),
                               ledManager->getIdlePattern()}, false)
{
}
