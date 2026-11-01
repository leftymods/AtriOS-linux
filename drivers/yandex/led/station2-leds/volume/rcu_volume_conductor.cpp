#include "rcu_volume_conductor.h"

#include <smart_devices/platforms/yandexstation_2/platformd/buttons_handler.h>

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>

RcuVolumeConductor::RcuVolumeConductor(std::string aliceGifLedAnimations,
                                       std::shared_ptr<quasar::LedManager> ledManager,
                                       std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController)
    : AnimationConductor({}, SubstitutionType::FOREGROUND, FinishingType::LONGEST)
    , rcuVolumeAnimationComposition_(
          std::make_shared<RcuVolumeAnimationComposition>(std::move(aliceGifLedAnimations), ledManager,
                                                          std::move(yandexStationFrontalController)))
{
    animationCompositionList_ = std::vector<const std::shared_ptr<AnimationComposition>>{rcuVolumeAnimationComposition_};
    resetConductor();
}

std::string RcuVolumeConductor::getName() const {
    return " <RcuVolumeConductor> ";
}

void RcuVolumeConductor::updateAnimation(double volume, bool muted, const std::string& source) {
    Y_VERIFY(0.0 <= volume);
    Y_VERIFY(volume <= 1.0);
    rcuVolumeAnimationComposition_->updateAnimation(volume, muted, source != ButtonsHandler::EventSource::RCU_HOLD);
    resetConductor();
}

void RcuVolumeConductor::updateVolume(double volume) {
    Y_VERIFY(0.0 <= volume);
    Y_VERIFY(volume <= 1.0);
    rcuVolumeAnimationComposition_->updateVolume(volume);
}
