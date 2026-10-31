#include "gif_animation.h"
#include "mics_state_led_player.h"

#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>
#include <yandex_io/modules/leds/led_patterns/led_pattern.h>
#include <yandex_io/libs/logging/logging.h>

#include <memory>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

MicsStateLedPlayer::MicsStateLedPlayer(std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices)
    : ledManager_(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
{
    Y_VERIFY(ledManager_.get() != nullptr);
}

void MicsStateLedPlayer::onMuted() {
    /* Animate fancy Mute animation */
    if (micsState_ == MicsState::UNMUTED) {
        try {
            auto defaultAnimationComposition = std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{
                    ledManager_->getPattern("mute_mics.led"),
                    GifAnimation::readGif(
                        ledDevices_->getDefaultAnimationsPath() + "mics/25x16_Mics_Off.gif",
                        ledDevices_->getLedScreen())},
                false);
            auto defaultAnimationConductor = std::make_shared<DefaultAnimationConductor>(defaultAnimationComposition);
            defaultAnimationConductor->setSubstitutionType(SubstitutionType::FOREGROUND);
            ledManager_->play(defaultAnimationConductor);
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("MicsStateLedPlayer.OnMuted.Exception", "failed to draw on led screen: " << e.what());
        }
    }
    micsState_ = MicsState::MUTED;
}

void MicsStateLedPlayer::onUnmuted() {
    if (micsState_ == MicsState::MUTED) {
        try {
            auto defaultAnimationComposition = std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{
                    GifAnimation::readGif(
                        ledDevices_->getDefaultAnimationsPath() + "mics/25x16_Mics_On.gif",
                        ledDevices_->getLedScreen())},
                false);
            auto defaultAnimationConductor = std::make_shared<DefaultAnimationConductor>(defaultAnimationComposition);
            defaultAnimationConductor->setSubstitutionType(SubstitutionType::FOREGROUND);
            ledManager_->play(defaultAnimationConductor);
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("MicsStateLedPlayer.OnUnmuted.Exception", "failed to draw on led screen: " << e.what());
        }
    }
    micsState_ = MicsState::UNMUTED;
}
