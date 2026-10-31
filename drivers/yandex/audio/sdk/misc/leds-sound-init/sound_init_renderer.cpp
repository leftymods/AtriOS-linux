#include "sound_init_renderer.h"

#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_patterns/progress_pattern.h>

#include <yandex_io/libs/logging/logging.h>

using namespace quasar;

SoundInitRenderer::SoundInitRenderer(std::shared_ptr<quasar::LedManager> ledManager)
    : ledManager_(std::move(ledManager))
{
}

void SoundInitRenderer::onSoundDataTransferStart()
{
    /* NOTE: Since SoundInit and Activation animation aren't part of DeviceState handler -> put them into separate
     * Group: "activation_group". (onSDKState use default empty group)
     */
    auto pattern = ledManager_->getPattern("soundinit.led");
    pattern->setDuration(30000);
    currentConductor_ = std::make_shared<StoppableAnimationConductor>(std::move(pattern));
    ledManager_->play(currentConductor_);
}

void SoundInitRenderer::onFinishSetup()
{
    /* Activation is finished */
    if (currentConductor_) {
        currentConductor_->stop();
    }
}

void SoundInitRenderer::onConnectingToNetwork()
{
    /* Replace soundinit.led with activating.led in activation_group */
    auto pattern = ledManager_->getPattern("activating.led");
    pattern->setDuration(60000);
    currentConductor_ = std::make_shared<StoppableAnimationConductor>(std::move(pattern));
    ledManager_->play(currentConductor_);
}

void SoundInitRenderer::onSetupError()
{
    /* return configure.led */
    auto pattern = ledManager_->getPattern("configure.led");
    currentConductor_ = std::make_shared<StoppableAnimationConductor>(std::move(pattern));
    ledManager_->play(currentConductor_);
    /* Play Error animation once */
    pattern = ledManager_->getPattern("error.led");
    pattern->setDuration(3000);
    ledManager_->play(std::make_shared<DefaultAnimationConductor>(std::move(pattern), SubstitutionType::FOREGROUND));
}
