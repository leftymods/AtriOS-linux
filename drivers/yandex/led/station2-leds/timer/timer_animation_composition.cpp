#include "timer_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

TimerAnimationComposition::TimerAnimationComposition(const std::string& aliceGifLedTemplates,
                                                     const std::string& fontBasePath,
                                                     std::shared_ptr<quasar::LedManager> ledManager,
                                                     std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                                                     std::chrono::system_clock::time_point firstTimerDueTime)
    : AnimationComposition({std::make_shared<TimerFrontalAnimation>(std::move(yandexStationFrontalController), aliceGifLedTemplates, fontBasePath, firstTimerDueTime),
                            ledManager->getIdlePattern()}, false)
{
}
