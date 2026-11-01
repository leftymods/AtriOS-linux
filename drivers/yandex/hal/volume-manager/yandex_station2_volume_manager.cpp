#include "buttons_handler.h"
#include "yandex_station2_volume_manager.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_patterns/playing_pattern.h>
#include <yandex_io/modules/leds/led_patterns/volume_pattern.h>

#include <cmath>

YandexStation2VolumeManager::YandexStation2VolumeManager(std::shared_ptr<YandexIO::IDevice> device,
                                                         const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
                                                         std::shared_ptr<YandexIO::SDKInterface> sdk,
                                                         const std::string& currentVolumeFilename,
                                                         const std::string& currentMuteStateFilename,
                                                         std::shared_ptr<quasar::LedManager> ledManager,
                                                         std::shared_ptr<VolumeConductor> volumeConductor,
                                                         std::shared_ptr<RcuVolumeConductor> rcuVolumeConductor,
                                                         const Json::Value& config)
    : VolumeManager(device, ipcFactory, std::move(sdk), currentVolumeFilename, currentMuteStateFilename, std::chrono::milliseconds(30))
    , ledManager_(std::move(ledManager))
    , toVolumeSetd_(ipcFactory->createIpcConnector("volume_setd"))
    , volumeConductor_(std::move(volumeConductor))
    , rcuVolumeConductor_(std::move(rcuVolumeConductor))
{
    Y_VERIFY(ledManager_ != nullptr);
    Y_VERIFY(volumeConductor_ != nullptr);
    Y_VERIFY(rcuVolumeConductor_ != nullptr);

    cfgAndroidVolume_.min = quasar::tryGetInt(config, "minAndroidVolume", DEFAULT_ANDROID_VOLUME.min);
    cfgAndroidVolume_.max = quasar::tryGetInt(config, "maxAndroidVolume", DEFAULT_ANDROID_VOLUME.max);
    androidVolume_ = cfgAndroidVolume_;

    toVolumeSetd_->setConnectHandler([this]() {
        std::lock_guard<std::mutex> guard(lastVolumeMutex_);
        sendLastVolume();
    });
    toVolumeSetd_->connectToService();
}

void YandexStation2VolumeManager::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName != quasar::ConfigUtils::MAIND_CONFIG) {
        return;
    }
    const auto config = quasar::tryParseJson(jsonConfigValue);
    if (config.has_value()) {
        handleConfigUpdate(config.value());
    }
}

void YandexStation2VolumeManager::handleConfigUpdate(const Json::Value& config) {
    androidVolume_.min = quasar::tryGetInt(config, "minAndroidVolumeV2", cfgAndroidVolume_.min);
    androidVolume_.max = quasar::tryGetInt(config, "maxAndroidVolumeV2", cfgAndroidVolume_.max);
    YIO_LOG_INFO("Update Android volume: min=" << androidVolume_.min << ", max=" << androidVolume_.max);
}

int YandexStation2VolumeManager::minVolume() {
    return 0;
}

int YandexStation2VolumeManager::maxVolume() {
    return RING_TICKS;
}

int YandexStation2VolumeManager::volumeStep() {
    return 1;
}

int YandexStation2VolumeManager::scaleFromAlice(int aliceVolume) {
    return std::round(aliceVolume * TICKS_PER_ALICE_VOLUME_STEP);
}

int YandexStation2VolumeManager::scaleToAlice(int platformVolume) {
    if (platformVolume == 0) {
        return 0;
    }
    return std::max<int>(std::round(platformVolume / TICKS_PER_ALICE_VOLUME_STEP), 1);
}

int YandexStation2VolumeManager::initialVolume() {
    return scaleFromAlice(3);
}

void YandexStation2VolumeManager::setVolumeImplementation(int platformVolume) {
    std::lock_guard<std::mutex> guard(lastVolumeMutex_);
    lastAliceVolume_ = scaleToAlice(platformVolume);
    lastAndroidVolume_ = convertToAndroidVolume(platformVolume);
    sendLastVolume();
}

void YandexStation2VolumeManager::playVolumeLed(int platformValue, bool muted, const std::string& source) {
    double volume = (double)platformValue / maxVolume();

    if (source == ButtonsHandler::EventSource::RCU_PRESS || source == ButtonsHandler::EventSource::RCU_HOLD) {
        rcuVolumeConductor_->updateAnimation(volume, muted, source);
        volumeConductor_->updateVolume(volume);
        ledManager_->play(rcuVolumeConductor_);
    } else {
        volumeConductor_->updateAnimation(volume, muted);
        rcuVolumeConductor_->updateVolume(volume);
        ledManager_->play(volumeConductor_);
    }
}

int YandexStation2VolumeManager::alarmRaisingPlatformVolumeStep() const {
    // Step is approximately 1/2 of Alice volume
    return static_cast<int>(std::trunc(TICKS_PER_ALICE_VOLUME_STEP / 2));
}

int YandexStation2VolumeManager::convertToAndroidVolume(int platformVolume) const {
    if (platformVolume == 0) {
        return 0;
    }

    // Step 1. Transform Platform volume to Alice volume
    // Long term yandexstation volume formula. Shouldn't be changed for user convenience reason
    float aliceVolume = (float)platformVolume / TICKS_PER_ALICE_VOLUME_STEP;

    // We don't want Android volume to be lower than minAndroidVolume_.
    // See: https://st.yandex-team.ru/ALICE-7568
    if (aliceVolume < 1.0) {
        return androidVolume_.min;
    }

    // Step 2. Transform Alice volume to Android volume
    const int minAliceVolume = 1;
    float step = (float)(androidVolume_.max - androidVolume_.min) / (MAX_ALICE_VOLUME - minAliceVolume);
    float androidVolume = androidVolume_.min + step * (aliceVolume - minAliceVolume);

    return (int)std::ceil(androidVolume);
}

void YandexStation2VolumeManager::sendLastVolume() {
    quasar::proto::QuasarMessage message;
    message.mutable_volume_set_message()->set_volume_value(lastAndroidVolume_);
    message.mutable_volume_set_message()->set_max_volume(androidVolume_.max);
    message.mutable_volume_set_message()->set_alice_volume_value(lastAliceVolume_);
    toVolumeSetd_->sendMessage(std::move(message));
}

// -------------- Bluetooth messages handling

void YandexStation2VolumeManager::onChangeVolumeAVRCP(int volumePercent) {
    /* We want to have pulse-like animations here, like we do with RCU, so we pass RCU_PRESS. */
    onChangeVolumeAVRCPInternal(volumePercent, ButtonsHandler::EventSource::RCU_PRESS);
}

void YandexStation2VolumeManager::subscribeToSystemConfig() {
    sdk_->subscribeToSystemConfig(quasar::ConfigUtils::MAIND_CONFIG);
}
