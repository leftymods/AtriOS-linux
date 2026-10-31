#include "configuration_mode_handler.h"

#include <yandex_io/sdk/alice_volume_setter.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/system/yassert.h>

#include <utility>

ConfigurationModeHandler::ConfigurationModeHandler(std::shared_ptr<YandexIO::SDKInterface> sdk,
                                                   std::shared_ptr<VolumeManager> volumeManager,
                                                   Settings settings)
    : settings_(std::move(settings))
    , sdk_(std::move(sdk))
    , volumeManager_(std::move(volumeManager))
{
    Y_VERIFY(volumeManager_);
}

void ConfigurationModeHandler::onStartSetup(bool firstTime) {
    if (setupMode_.value_or(false)) {
        return;
    }
    setupMode_ = true;

    playConfigurationSound(firstTime);
}

void ConfigurationModeHandler::onFinishSetup() {
    if (!setupMode_.value_or(false)) {
        return;
    }

    setupMode_ = false;
    stopSound();
}

void ConfigurationModeHandler::subscribeToSDK() {
    sdk_->addDeviceModeObserver(shared_from_this());
}

void ConfigurationModeHandler::playConfigurationSound(bool firstTime) {
    /* Configure volume before playing wav/mp3 file */
    if (firstTime) {
        /* First time init. Always set up default volume */
        // FIXME: it's a dirty hack to access AliceVolumeSetter::setVolume
        std::shared_ptr<YandexIO::AliceVolumeSetter> aliceVolumeSetter = volumeManager_;
        aliceVolumeSetter->setVolume(settings_.firstInitDefaultVolume, false, YandexIO::AliceVolumeSetter::SOURCE_OTHER);
        playSound(settings_.enterConfigurationModeFirstTimeSound);
    } else {
        const int currentVolume = volumeManager_->scaleToAlice(volumeManager_->manualGetCurrentVolume());
        if (currentVolume < settings_.initModeMinimumVolume) {
            volumeManager_->manualSetVolume(volumeManager_->scaleFromAlice(currentVolume), VolumeManager::SOURCE_OTHER);
        }
        playSound(settings_.enterConfigurationModeSound);
    }
}

void ConfigurationModeHandler::playSound(const std::string& wavFile) {
    currentSound_ = wavFile;
    sdk_->getFilePlayerCapability()->playSoundFile(currentSound_, std::nullopt);
}

void ConfigurationModeHandler::stopSound() {
    if (currentSound_.empty()) {
        return;
    }
    sdk_->getFilePlayerCapability()->stopSoundFile(currentSound_);
    currentSound_.clear();
}
