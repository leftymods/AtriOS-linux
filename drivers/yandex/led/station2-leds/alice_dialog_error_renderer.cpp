#include "alice_dialog_error_renderer.h"
#include "gif_animation.h"
#include "repeated_animation.h"

#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>
#include <yandex_io/modules/leds/led_patterns/led_pattern.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

AliceDialogErrorRenderer::AliceDialogErrorRenderer(std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices)
    : ledManager_(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
{
}

void AliceDialogErrorRenderer::onConversationError()
{
    /* Play Error animation  */
    auto errorPattern = ledManager_->getPattern("error.led");
    errorPattern->setDuration(3000);
    try {
        auto defaultAnimationComposition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{
                errorPattern,
                std::make_shared<RepeatedAnimation>(GifAnimation::readGif(
                                                        ledDevices_->getDefaultAnimationsPath() + "25x16_Error.gif",
                                                        ledDevices_->getLedScreen()), 3)}, false);
        auto defaultAnimationConductor = std::make_shared<DefaultAnimationConductor>(defaultAnimationComposition);
        defaultAnimationConductor->setSubstitutionType(SubstitutionType::FOREGROUND);
        ledManager_->play(defaultAnimationConductor);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("AliceDialogErrorRenderer.Exception", "failed to draw on led screen: " << e.what());
    }
}

void AliceDialogErrorRenderer::onSDKState(const YandexIO::SDKState& /* state */) {
    // no-op
}
