#include "empty_animation.h"
#include "gif_animation.h"
#include "media_state_led_player.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>

#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>
#include <yandex_io/modules/leds/led_patterns/led_pattern.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <memory>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

namespace {
    const bool DISABLE_SWITCH_TRACK_ANIMATION = false;
} // namespace

MediaStateLedPlayer::MediaStateLedPlayer(std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices)
    : ledManager_(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
    , disableSwitchTrackAnimation_(DISABLE_SWITCH_TRACK_ANIMATION)
{
    Y_VERIFY(ledManager_.get() != nullptr);
}

void MediaStateLedPlayer::onMediaError(ContentType /* contentType */) {
    auto errorPattern = ledManager_->getPattern("error.led");
    errorPattern->setDuration(3000);
    try {
        auto defaultAnimationComposition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{
                errorPattern,
                GifAnimation::readGif(
                    ledDevices_->getDefaultAnimationsPath() + "media/25x16_Error.gif",
                    ledDevices_->getLedScreen())},
            false);
        auto defaultAnimationConductor = std::make_shared<DefaultAnimationConductor>(defaultAnimationComposition);
        defaultAnimationConductor->setSubstitutionType(SubstitutionType::FOREGROUND);
        ledManager_->play(defaultAnimationConductor);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("MediaStateLedPlayer.OnMediaError.Exception", "failed to draw on led screen: " << e.what());
    }
}

void MediaStateLedPlayer::onMediaPaused(ContentType /* contentType */) {
    playFrontalAnimation("media/25x16_Pause.gif");
}

void MediaStateLedPlayer::onMediaSwitchedForward(ContentType /* contentType */) {
    if (!disableSwitchTrackAnimation_) {
        playFrontalAnimation("media/25x16_Next.gif");
    }
}

void MediaStateLedPlayer::onMediaSwitchedBackward(ContentType /* contentType */) {
    if (!disableSwitchTrackAnimation_) {
        playFrontalAnimation("media/25x16_Previous.gif");
    }
}

void MediaStateLedPlayer::onMediaLiked(ContentType /* contentType */) {
    playFrontalAnimation("media/25x16_Like.gif");
}

void MediaStateLedPlayer::onMediaDisliked(ContentType /* contentType */) {
    playFrontalAnimation("media/25x16_Dislike.gif");
}

void MediaStateLedPlayer::onMediaResumed(ContentType /* contentType */) {
    playFrontalAnimation("media/25x16_Resume.gif");
}

void MediaStateLedPlayer::playFrontalAnimation(const std::string& animation) {
    std::vector<const std::shared_ptr<AnimationComposition>> compositions;
    try {
        compositions.push_back(std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{
                GifAnimation::readGif(ledDevices_->getDefaultAnimationsPath() + animation, ledDevices_->getLedScreen())},
            false));

        auto conductor = std::make_shared<DefaultAnimationConductor>(compositions);
        conductor->setSubstitutionType(SubstitutionType::FOREGROUND);
        ledManager_->play(conductor);

    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("MediaStateLedPlayer.PlayFrontalAnimation.Exception", "failed to draw on led screen: " << e.what());
    }
}

void MediaStateLedPlayer::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName == quasar::ConfigUtils::MAIND_CONFIG) {
        const auto config = quasar::tryParseJson(jsonConfigValue).value_or(Json::nullValue);
        disableSwitchTrackAnimation_ = quasar::tryGetBool(config, "disableSwitchTrackAnimation", DISABLE_SWITCH_TRACK_ANIMATION);
    }
}
