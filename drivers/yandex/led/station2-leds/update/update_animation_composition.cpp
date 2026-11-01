#include "progress_update_gif.h"
#include "update_animation_composition.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

#include <yandex_io/libs/logging/logging.h>

UpdateAnimationComposition::UpdateAnimationComposition(
    const std::string& aliceGifLedAnimations,
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
    const std::shared_ptr<quasar::LedManager>& ledManager)
    : AnimationComposition({}, false)
{
    updateProgressGif_ =
        std::make_shared<ProgressUpdateGif>(std::move(yandexStationFrontalController),
                                            aliceGifLedAnimations + "25x16_Progress_1.gif");
    updateProgressPattern_ = ledManager->getProgressPattern();
    updateProgressPattern_->setProgress(0.0);
}

void UpdateAnimationComposition::setProgress(int downloadProgress) {
    updateProgressPattern_->setProgress(downloadProgress);
    updateProgressGif_->setProgress(downloadProgress);
}

std::vector<std::shared_ptr<Animation>> UpdateAnimationComposition::getAnimations() {
    return {updateProgressPattern_, updateProgressGif_};
}
