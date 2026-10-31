#include "update_animation_conductor.h"
#include "update_animation_composition.h"

#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>

#include <memory>
#include <vector>

UpdateAnimationConductor::UpdateAnimationConductor(const std::string& aliceGifLedAnimations,
                                                   std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                                                   const std::shared_ptr<quasar::LedManager>& ledManager)
    : AnimationConductor({})
{
    updateComposition_ = std::make_shared<UpdateAnimationComposition>(aliceGifLedAnimations,
                                                                      std::move(yandexStationFrontalController),
                                                                      ledManager);
    animationCompositionList_ = std::vector<const std::shared_ptr<AnimationComposition>>{updateComposition_};
    resetConductor();
}

std::string UpdateAnimationConductor::getName() const {
    return " <UpdateAnimationConductor> ";
}

void UpdateAnimationConductor::setProgress(int downloadProgress) {
    updateComposition_->setProgress(downloadProgress);
}
