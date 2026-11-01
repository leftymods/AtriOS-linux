#include "volume_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

VolumeAnimationComposition::VolumeAnimationComposition(const std::string& aliceGifLedAnimations,
                                                       std::shared_ptr<quasar::LedManager> ledManager,
                                                       std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController)
    : AnimationComposition({}, false)
    , volumePattern_(std::make_shared<Station2VolumePattern>(ledManager->getPattern("volume.led")))
    , volumeFrontalAnimation_(std::make_shared<VolumeFrontalAnimation>(aliceGifLedAnimations + "whole_volume.gif", yandexStationFrontalController))
    , volumeMuteFrontalAnimation_(GifAnimation::readGif(aliceGifLedAnimations + "sound_mute.gif", yandexStationFrontalController))
{
}

void VolumeAnimationComposition::updateAnimation(double volume, bool muted) {
    std::scoped_lock guard(mutex_);
    volumePattern_->updateVolume(volume);
    volumeFrontalAnimation_->updateAnimation(volume);
    volumeMuteFrontalAnimation_->resetAnimation(); // TODO race here. copy volumeMuteFrontalAnimation_ instead, so that is a clean copy protected by mutex_
    if (muted) {
        currentFrontalAnimation_ = volumeMuteFrontalAnimation_;
    } else {
        currentFrontalAnimation_ = volumeFrontalAnimation_;
    }
}

void VolumeAnimationComposition::updateVolume(double volume) {
    std::scoped_lock guard(mutex_);
    volumeFrontalAnimation_->updateVolume(volume);
}

std::vector<std::shared_ptr<Animation>> VolumeAnimationComposition::getAnimations() {
    std::scoped_lock guard(mutex_);
    return {volumePattern_, currentFrontalAnimation_};
}
