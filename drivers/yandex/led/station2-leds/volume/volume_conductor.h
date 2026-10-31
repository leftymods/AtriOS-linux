#pragma once

#include "volume_animation_composition.h"

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>

class VolumeConductor: public AnimationConductor {
public:
    VolumeConductor(const std::string& aliceGifLedAnimations,
                    std::shared_ptr<quasar::LedManager> ledManager,
                    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController);

    std::string getName() const override;

    void updateAnimation(double volume, bool muted);

    void updateVolume(double volume);

private:
    std::shared_ptr<VolumeAnimationComposition> volumeAnimationComposition_;
};
