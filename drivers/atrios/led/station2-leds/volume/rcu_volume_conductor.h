#pragma once

#include "rcu_volume_animation_composition.h"

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>
#include <yandex_io/modules/volume_manager/base/volume_manager.h>

class RcuVolumeConductor: public AnimationConductor {
public:
    RcuVolumeConductor(std::string aliceGifLedAnimations,
                       std::shared_ptr<quasar::LedManager> ledManager,
                       std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController);

    std::string getName() const override;

    void updateAnimation(double volume, bool muted, const std::string& source);

    void updateVolume(double volume);

private:
    std::shared_ptr<RcuVolumeAnimationComposition> rcuVolumeAnimationComposition_;
};
