#include "volume_conductor.h"

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>

VolumeConductor::VolumeConductor(const std::string& aliceGifLedAnimations,
                                 std::shared_ptr<quasar::LedManager> ledManager,
                                 std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController)
    : AnimationConductor({}, SubstitutionType::FOREGROUND)
    , volumeAnimationComposition_(std::make_shared<VolumeAnimationComposition>(aliceGifLedAnimations, ledManager,
                                                                               std::move(yandexStationFrontalController)))
{
    animationCompositionList_ = std::vector<const std::shared_ptr<AnimationComposition>>{volumeAnimationComposition_};
    resetConductor();
}

std::string VolumeConductor::getName() const {
    return " <VolumeConductor> ";
}

void VolumeConductor::updateAnimation(double volume, bool muted) {
    Y_VERIFY(0.0 <= volume);
    Y_VERIFY(volume <= 1.0);
    volumeAnimationComposition_->updateAnimation(volume, muted);
    resetConductor();
}

void VolumeConductor::updateVolume(double volume) {
    Y_VERIFY(0.0 <= volume);
    Y_VERIFY(volume <= 1.0);
    volumeAnimationComposition_->updateVolume(volume);
}
