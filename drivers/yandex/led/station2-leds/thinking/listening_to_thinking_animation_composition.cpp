#include "listening_to_thinking_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

ListeningToThinkingAnimationComposition::ListeningToThinkingAnimationComposition(
    const std::shared_ptr<quasar::LedPattern>& ringThinkingPattern,
    const std::string& aliceGifLedAnimations,
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
    const std::shared_ptr<quasar::LedManager>& ledManager [[maybe_unused]])
    : AnimationComposition(std::vector<std::shared_ptr<Animation>>{
                               GifAnimation::readGif(aliceGifLedAnimations + "oknix/04-listening-to-answer.gif",
                                                     yandexStationFrontalController),
                               ringThinkingPattern}, false)
{
}
