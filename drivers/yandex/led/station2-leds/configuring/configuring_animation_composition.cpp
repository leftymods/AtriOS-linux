#include "configuring_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

ConfiguringAnimationComposition::ConfiguringAnimationComposition(
    const std::string& aliceGifLedAnimations,
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
    const std::shared_ptr<quasar::LedManager>& /* ledManager */,
    std::shared_ptr<Animation> configurePattern)
    : AnimationComposition(std::vector<std::shared_ptr<Animation>>{
                               GifAnimation::readGif(aliceGifLedAnimations + "boot/25x16_FirstBoot_MainBody.gif",
                                                     yandexStationFrontalController),
                               std::move(configurePattern)}, false)
{
}
