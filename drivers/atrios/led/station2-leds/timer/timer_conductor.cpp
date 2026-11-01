#include "timer_conductor.h"

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>

TimerConductor::TimerConductor(const std::string& aliceGifLedTemplates,
                               const std::string& fontBasePath,
                               std::shared_ptr<quasar::LedManager> ledManager,
                               std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                               std::chrono::system_clock::time_point firstTimerDueTime)
    : AnimationConductor({std::make_shared<TimerAnimationComposition>(aliceGifLedTemplates, fontBasePath, std::move(ledManager),
                                                                      std::move(yandexStationFrontalController),
                                                                      firstTimerDueTime)})
{
}

std::string TimerConductor::getName() const {
    return " <TimerConductor> ";
}
