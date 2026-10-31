#include "gif_animation.h"
#include "led_directive_observer.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/cache_manager.h>

#include <yandex_io/libs/base/directives.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

LedDirectiveObserver::LedDirectiveObserver(std::shared_ptr<quasar::LedManager> ledManager,
                                           std::shared_ptr<LedDevicesImpl> ledDevices,
                                           std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenObserver,
                                           std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder)
    : ledManager_(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
    , drawLedScreenObserver_(std::move(drawLedScreenObserver))
    , clockDisplayStateHolder_(std::move(clockDisplayStateHolder))
{
}

LedDirectiveObserver::~LedDirectiveObserver() {
    callbackQueue_.destroy();
    disabledDirectives_.clear();
}

void LedDirectiveObserver::onDirective(const std::string& name, const std::string& /* vinsRequestId */, const std::string& jsonPayload) {
    if (name == Directives::DRAW_LED_SCREEN) {
        // there is separate handler for that custom directive animation
        drawLedScreenObserver_->onDrawLedScreenDirective(name, jsonPayload);
        return;
    }

    callbackQueue_.add([this, name] {
        processDirective(name);
    });
}

void LedDirectiveObserver::onSystemConfig(const std::string& configName, const std::string& jsonPayload) {
    callbackQueue_.add([this, configName, jsonPayload] {
        if (configName == ConfigUtils::DIRECTIVE_ANIMATION_CONFIG) {
            updateDisabledDirectives(jsonPayload);
        }
        if (configName == ConfigUtils::ANIMATIONS_CACHE_CONFIG) {
            updateCacheParameters(jsonPayload);
        }
    });
}

void LedDirectiveObserver::updateDeviceState(YandexIO::ExtendedDeviceState deviceState) {
    deviceState_ = std::move(deviceState);
}

void LedDirectiveObserver::processDirective(const std::string& name) {
    if (name == quasar::Directives::SHOW_CLOCK || name == quasar::Directives::HIDE_CLOCK) {
        const auto state = clockDisplayStateHolder_->getClockDisplayState();
        const bool enabled = name == quasar::Directives::SHOW_CLOCK;
        // directive has priority over dnd. So turn on clock after directive even if currently in dnd mode
        clockDisplayStateHolder_->setClockBehaviour(enabled, enabled, state.showClockDuringDnd);
        return;
    }

    auto composition = getLocalComposition(name);
    if (composition == nullptr) {
        // it's ok to not have local composition for some directives, so just skip it
        return;
    }

    YIO_LOG_INFO("Draw directive animation for " << name);
    auto makeCompositionVector = [](auto t) -> std::vector<const std::shared_ptr<AnimationComposition>> {
        return {std::move(t)};
    };
    auto conductor = std::make_shared<DefaultAnimationConductor>(makeCompositionVector(std::move(composition)));
    conductor->setSubstitutionType(SubstitutionType::FOREGROUND);
    ledManager_->play(conductor);
}

std::shared_ptr<AnimationComposition> LedDirectiveObserver::getLocalComposition(const std::string& directiveName) {
    std::string gifName;
    if (directiveName == Directives::PLAYER_LIKE) {
        gifName = "media/25x16_Like_Fast.gif";
    } else if (directiveName == Directives::PLAYER_DISLIKE) {
        gifName = "media/25x16_Dislike.gif";
    } else if (directiveName == Directives::PLAYER_NEXT_TRACK) {
        gifName = "media/25x16_Next.gif";
    } else if (directiveName == Directives::PLAYER_PREVIOUS_TRACK) {
        gifName = "media/25x16_Previous.gif";
    } else if (directiveName == Directives::PLAYER_CONTINUE) {
        gifName = "media/25x16_Resume.gif";
    } else if (directiveName == Directives::PLAYER_PAUSE && deviceState_.isMediaPlaying()) { // Draw pause gif only if media is playing
        gifName = "media/25x16_Pause.gif";
    }

    // if we have no gif, or directive was disabled in config
    if (gifName.empty() || disabledDirectives_.count(directiveName)) {
        return nullptr;
    }

    return makeGifAnimationOverSpeaking(gifName);
}

std::shared_ptr<AnimationComposition> LedDirectiveObserver::makeGifAnimationOverSpeaking(const std::string& gifName) {
    auto speakingPattern = ledManager_->getPattern("speaking.led");
    try {
        const auto path = ledDevices_->getDefaultAnimationsPath() + gifName;
        auto animation = GifAnimation::readGif(path, ledDevices_->getLedScreen());
        auto composition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{std::move(animation), speakingPattern},
            false);
        return composition;
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("LedDirectiveObserver.MakeGifAnimation.Exception", "Can't create " << gifName << " animation: " << e.what());
    }
    return nullptr;
}

void LedDirectiveObserver::updateDisabledDirectives(const std::string& jsonPayload) {
    disabledDirectives_.clear();
    auto config = tryParseJson(jsonPayload);
    if (config) {
        auto animationConfig = config.value();
        Json::Value disabledDirectives = animationConfig.get("disabledDirectives", Json::arrayValue);
        for (const Json::Value& disabled : disabledDirectives) {
            disabledDirectives_.insert(disabled.asString());
        }
        YIO_LOG_INFO("updateDisabledDirectives with " << jsonPayload);
    }
}

void LedDirectiveObserver::updateCacheParameters(const std::string& jsonPayload) {
    auto cacheConfig = tryParseJson(jsonPayload).value_or(Json::nullValue);
    const int sizeKbytes = quasar::tryGetInt(cacheConfig, "sizeKbytes", CacheManager::DEFAULT_SIZE_KBYTES);
    const uint64_t sizeEntries = quasar::tryGetUInt64(cacheConfig, "sizeEntries", CacheManager::DEFAULT_SIZE_ENTRIES);
    drawLedScreenObserver_->setSizeKbytes(sizeKbytes);
    drawLedScreenObserver_->setSizeEntries(sizeEntries);
    YIO_LOG_INFO("Cache params were updated sizeKbytes=" << sizeKbytes << " sizeEntries=" << sizeEntries);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void LedDirectiveObserver::subscribeToSystemConfig(YandexIO::SDKInterface& sdk) {
    sdk.subscribeToSystemConfig(ConfigUtils::DIRECTIVE_ANIMATION_CONFIG);
    sdk.subscribeToSystemConfig(ConfigUtils::ANIMATIONS_CACHE_CONFIG);
}
