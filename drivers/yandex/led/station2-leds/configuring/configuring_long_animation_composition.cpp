#include "configuring_long_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/repeated_animation.h>

#include <utility>

ConfiguringLongAnimationComposition::ConfiguringLongAnimationComposition(
    const std::string& aliceGifLedAnimations,
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
    const std::shared_ptr<quasar::LedManager>& /* ledManager */,
    std::shared_ptr<Animation> configuringPattern)

    : AnimationComposition(std::vector<std::shared_ptr<Animation>>{
                               std::make_shared<RepeatedAnimation>(
                                   GifAnimation::readGif(aliceGifLedAnimations + "scenario_emotion/25x16_Listening_Loop_Output.gif",
                                                         yandexStationFrontalController), RepeatedAnimation::INFINITE),
                               std::move(configuringPattern)}, false)
{
}
