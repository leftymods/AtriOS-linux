#pragma once

#include "progress_update_gif.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_composition.h>

class UpdateAnimationComposition: public AnimationComposition {
public:
    UpdateAnimationComposition(const std::string& aliceGifLedAnimations,
                               std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                               const std::shared_ptr<quasar::LedManager>& ledManager);

    void setProgress(int downloadProgress);

private:
    std::shared_ptr<quasar::ProgressPattern> updateProgressPattern_;
    std::shared_ptr<ProgressUpdateGif> updateProgressGif_;

public:
    std::vector<std::shared_ptr<Animation>> getAnimations() override;
};
