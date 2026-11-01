#include "rcu_volume_animation_composition.h"

RcuVolumeAnimationComposition::RcuVolumeAnimationComposition(std::string aliceGifLedAnimations,
                                                             std::shared_ptr<quasar::LedManager> ledManager,
                                                             std::weak_ptr<YandexStationFrontalController> yandexStationFrontalController)
    : AnimationComposition({}, false)
    , volumePattern_(std::make_shared<Station2VolumePattern>(ledManager->getPattern("volume.led")))
    , volumeFrontalAnimation_(std::make_shared<RcuVolumeFrontalAnimation>(
          std::move(aliceGifLedAnimations), std::move(yandexStationFrontalController)))
{
}

void RcuVolumeAnimationComposition::updateAnimation(double volume, bool muted, bool showPulse) {
    volumePattern_->updateVolume(volume);
    volumeFrontalAnimation_->updateAnimation(volume, muted, showPulse);
}

void RcuVolumeAnimationComposition::updateVolume(double volume) {
    volumeFrontalAnimation_->updateVolume(volume);
}

std::vector<std::shared_ptr<Animation>> RcuVolumeAnimationComposition::getAnimations() {
    auto currentFrontalAnimation = volumeFrontalAnimation_->getCurrentAnimation();
    if (currentFrontalAnimation == nullptr) {
        return {volumePattern_};
    }
    return {volumePattern_, currentFrontalAnimation};
}
