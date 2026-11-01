#include "demo_mode_front_led_handler.h"
#include "gif_animation.h"
#include "empty_animation.h"
#include "clock/clock_animation.h"

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_manager/ng/idle_animation_composition.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

DemoModeFrontLedHandler::DemoModeFrontLedHandler(std::shared_ptr<quasar::LedManager> ledManager,
                                                 std::shared_ptr<LedDevicesImpl> ledDevices,
                                                 const std::string& aliceFrontalAnimationFontsPath)
    : DemoModeLedHandler(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
    , fontConfig_(ConfigUtils::getClockFont(Json::Value(), aliceFrontalAnimationFontsPath))
{
}

void DemoModeFrontLedHandler::onDemoModeItemPlaying(const DemoItem& item) {
    if (!item.animation.has_value()) {
        return;
    }
    try {
        auto inComposition = getAnimationComposition(item.animation->in.cachedFile, true);
        auto loopComposition = getAnimationComposition(item.animation->loop.cachedFile, true, true);

        auto conductor = std::make_shared<DefaultAnimationConductor>(
            std::vector<const std::shared_ptr<AnimationComposition>>{
                inComposition, loopComposition},
            SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<onStoryStarted>");
        YIO_LOG_INFO("Conductor to play: " << conductor->getName());
        ledManager_->play(std::move(conductor));
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("DemoModeFrontLedHandler.Exception", "failed to draw on led screen: " << e.what());
    }
}

void DemoModeFrontLedHandler::onDemoModeItemFinished(const DemoItem& item, bool havePendingStories) {
    if (!item.animation.has_value()) {
        return;
    }

    std::vector<const std::shared_ptr<AnimationComposition>> compositions;
    compositions.push_back(getAnimationComposition(item.animation->out.cachedFile));

    if (havePendingStories) {
        // insert 5 sec empty animation to prevent showing clock between stories
        // next story will replace empty animation right away
        auto fiveSecEmptyAnimation = std::make_shared<EmptyAnimation>(ledDevices_->getLedScreen());
        const auto tp = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        fiveSecEmptyAnimation->setFinishedTimepoint(tp);
        auto emptyComposition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{fiveSecEmptyAnimation, ledManager_->getIdlePattern()},
            false);
        compositions.push_back(std::move(emptyComposition));
    }

    auto clockAnimation = std::make_shared<ClockAnimation>(ledDevices_->getLedScreen(), fontConfig_);
    compositions.push_back(std::make_shared<IdleAnimationComposition>(std::move(clockAnimation), *ledManager_));

    auto conductor = std::make_shared<DefaultAnimationConductor>(std::move(compositions), SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<onStoryFinished>");
    YIO_LOG_INFO("Conductor to play: " << conductor->getName());
    ledManager_->play(std::move(conductor));
}

std::shared_ptr<AnimationComposition> DemoModeFrontLedHandler::getAnimationComposition(const std::string& filePath, bool speaking, bool looped) const {
    std::shared_ptr<Animation> gif;
    try {
        gif = GifAnimation::readGif(filePath, ledDevices_->getLedScreen());
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("DemoModeFrontLedHandler.ReadGif.Exception", "Can't parse gif animation " << filePath << ", error: " << e.what());
    }

    std::vector<std::shared_ptr<Animation>> animations;
    if (gif) {
        animations.push_back(std::move(gif));
        animations.push_back(speaking ? ledManager_->getPattern("speaking.led") : ledManager_->getIdlePattern());
    }
    return std::make_shared<DefaultAnimationComposition>(std::move(animations), looped);
}
