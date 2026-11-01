#pragma once

#include "update_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>

class UpdateAnimationConductor: public AnimationConductor {
public:
    UpdateAnimationConductor(const std::string& aliceGifLedAnimations,
                             std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                             const std::shared_ptr<quasar::LedManager>& ledManager);

    std::string getName() const override;

    void setProgress(int downloadProgress);

private:
    std::shared_ptr<UpdateAnimationComposition> updateComposition_;
    const std::shared_ptr<UpdateAnimationComposition> setUpdateComposition(std::shared_ptr<UpdateAnimationComposition> sharedPtr);
};
