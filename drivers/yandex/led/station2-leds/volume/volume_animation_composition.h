#pragma once

#include "volume_frontal_animation.h"
#include "head/station2_volume_pattern.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_composition.h>
#include <yandex_io/modules/leds/led_patterns/volume_pattern.h>

#include <mutex>

class VolumeAnimationComposition: public AnimationComposition {
public:
    VolumeAnimationComposition(const std::string& aliceGifLedAnimations,
                               std::shared_ptr<quasar::LedManager> ledManager,
                               std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController);

    void updateAnimation(double volume, bool muted);

    void updateVolume(double volume);

    std::vector<std::shared_ptr<Animation>> getAnimations() override;

private:
    std::shared_ptr<Station2VolumePattern> volumePattern_;
    std::shared_ptr<VolumeFrontalAnimation> volumeFrontalAnimation_;
    std::shared_ptr<Animation> volumeMuteFrontalAnimation_;
    std::shared_ptr<Animation> currentFrontalAnimation_;
    std::mutex mutex_;
};
