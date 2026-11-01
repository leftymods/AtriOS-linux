#include "volume_manager.h"

#include "volume_metrics.h"

#include <yandex_io/libs/base/persistent_file.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/configuration/configuration.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/debug.h>
#include <yandex_io/libs/threading/utils.h>

#include <yandex_io/protos/quasar_proto.pb.h>

#include <util/system/yassert.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <utility>

YIO_DEFINE_LOG_MODULE("volume_manager");

using namespace quasar;

VolumeManager::VolumeManager(std::shared_ptr<YandexIO::IDevice> device,
                             std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
                             std::shared_ptr<YandexIO::SDKInterface> sdk,
                             std::string currentVolumeFilename,
                             std::string currentMuteStateFilename,
                             std::chrono::milliseconds periodOfSetting)
    : device_(std::move(device))
    , currentVolumeFilename_(std::move(currentVolumeFilename))
    , currentMuteStateFilename_(std::move(currentMuteStateFilename))
    , periodOfSetting_(periodOfSetting)
    , metricsVolumeListener_(std::make_shared<VolumeMetrics>(device_->telemetry()))
    , volumeManagerServer_(ipcFactory->createIpcServer("volume_manager"))
    , screenSaverDecreaseVolumeTimer_([this]() {
        std::scoped_lock guard(mutex_);
        if (isScreensaverOn_ && !isMediaPlaying_) {
            setAliceVolumeNotMoreUnlocked(5);
        }
    })
    , sdk_(std::move(sdk))
{
    Y_VERIFY(periodOfSetting_.count() >= 0);

    const auto& alarmConfig = device_->configuration()->getServiceConfig("alarmd");
    alarmsSettings_.startVolume = quasar::tryGetInt(alarmConfig, "startAlarmUserVolume", 2);
    alarmsSettings_.finishVolume = quasar::tryGetInt(alarmConfig, "finishAlarmUserVolume", 7);
    alarmsSettings_.volumeRaiseStepMs = quasar::tryGetInt(alarmConfig, "alarmVolumeStepMs", 20000);

    minReminderAliceVolume_ = quasar::tryGetInt(alarmConfig, "minimumReminderUserVolume", 1);

    bluetoothStateListener_ = std::make_shared<YandexIO::CallbackBluetoothStateListener>([this](bool state) {
        std::scoped_lock guard(mutex_);
        isBluetoothPlaying_ = state;
        if (isBluetoothPlaying_ != isMediaPlaying_) {
            processMediaStateOnScreenSaver(isScreensaverOn_, isBluetoothPlaying_);
            isMediaPlaying_ = isBluetoothPlaying_;
        } }, [](bool /*isConnected*/) {});

    // todo: make metricsVolumeListener_ external for VolumeManager
    addListener(metricsVolumeListener_);

    volumeManagerServer_->setMessageHandler(
        [this](const auto& message, auto& /*connection*/) {
            if (message->has_volume_manager_message() && message->volume_manager_message().has_set_volume_signal()) {
                const auto& svs = message->volume_manager_message().set_volume_signal();
                if (svs.has_platform_volume()) {
                    setPlatformVolume(svs.platform_volume(), svs.is_muted(), svs.source());
                } else if (svs.has_alice_volume()) {
                    setAliceVolume(svs.alice_volume(), svs.is_muted(), svs.source());
                }
            }
        });
    volumeManagerServer_->setClientConnectedHandler(
        [this](auto& /*connection*/) {
            std::lock_guard lock(mutex_);
            sendVolumeManagerStateUnlocked();
        });
}

VolumeManager::~VolumeManager() {
    volumeManagerServer_->shutdown();
    raisingVolumeExecutor_.reset();

    std::unique_lock<std::mutex> lock(mutex_);
    isRunning_ = false;
    callbackQueue_ = std::queue<CallbackItem>{};
    CV_.notify_one();
    lock.unlock();

    if (volumeSettingThread_.joinable()) {
        volumeSettingThread_.join();
    }
}

void VolumeManager::addListener(std::shared_ptr<IVolumeManagerListener> listener) {
    std::lock_guard<std::mutex> guard(mutex_);

    listeners_.push_back(listener);

    if (isRunning_) {
        listener->onVolumeChange(currentVolume_, scaleToAlice(currentVolume_), isMuted_, SOURCE_OTHER, true);
    }
}

void VolumeManager::start() {
    std::lock_guard<std::mutex> guard(mutex_);

    if (isRunning_) {
        throw std::runtime_error("Already started");
    }

    // Load previous saved volume
    int volume;
    bool isMuted;
    loadVolumeState(volume, isMuted);
    YIO_LOG_INFO("Restored values: volume " << volume << ", isMuted " << isMuted);

    isRunning_ = true;
    scheduleSetVolumeUnlocked(volume, isMuted, Animate::DO_NOT_ANIMATE, Tone::DO_NOT_PLAY_TONE, SendAVRCP::SEND_AVRCP, SOURCE_OTHER);

    volumeSettingThread_ = std::thread(&VolumeManager::volumeSettingThread, this);

    sdk_->addAlarmObserver(shared_from_this());
    sdk_->addNotificationObserver(shared_from_this());
    sdk_->addSDKStateObserver(shared_from_this());

    volumeManagerServer_->listenService();
}

void VolumeManager::enableStashVolumeMode(int platformVolume) {
    YIO_LOG_INFO("Enable stash volume mode " << platformVolume);
    std::scoped_lock lock(mutex_);
    stash(currentVolume_);
    scheduleSetVolumeUnlocked(platformVolume, false, Animate::DO_NOT_ANIMATE, Tone::DO_NOT_PLAY_TONE, SendAVRCP::SEND_AVRCP, "StashVolumeMode");
    scheduleCallbackUnlock([this] {
        std::scoped_lock lock(mutex_);
        stashedVolumeMode_ = true;
    });
}

void VolumeManager::disableStashVolumeMode() {
    YIO_LOG_INFO("Disable stash volume mode");
    std::scoped_lock lock(mutex_);
    if (std::exchange(stashedVolumeMode_, false)) {
        unstash();
    }
}

// -------------- Volume changes from user side

void VolumeManager::manualMute(const std::string& source) {
    stopRaisingVolume();
    std::lock_guard<std::mutex> guard(mutex_);
    muteUnlocked(source);
}

void VolumeManager::manualUnmute(const std::string& source) {
    stopRaisingVolume();
    std::lock_guard<std::mutex> guard(mutex_);
    unmuteUnlocked(source);
}

void VolumeManager::manualSetVolume(int volume, const std::string& source) {
    setPlatformVolume(volume, false, source);
}

void VolumeManager::manualVolumeUp(const std::string& source) {
    manualVolumeChange(1, source);
}

void VolumeManager::manualVolumeDown(const std::string& source) {
    manualVolumeChange(-1, source);
}

void VolumeManager::manualVolumeChange(int diff, const std::string& source) {
    stopRaisingVolume();
    scheduleSetVolume(volumeToSet_ + diff, false, Animate::ANIMATE, Tone::PLAY_TONE,
                      SendAVRCP::SEND_AVRCP, source);
}

int VolumeManager::manualGetCurrentVolume() {
    std::lock_guard<std::mutex> guard(mutex_);
    return currentVolume_;
}

bool VolumeManager::manualGetIsMuted() {
    std::lock_guard<std::mutex> guard(mutex_);
    return isMuted_;
}

void VolumeManager::setAliceVolumeMaxLimit(std::optional<int> aliceLimit) {
    std::optional<int> platformLimit;
    if (aliceLimit) {
        platformLimit = scaleFromAlice(std::clamp(aliceLimit.value(), 0, MAX_ALICE_VOLUME));
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (maxPlatformVolume_ != platformLimit) {
        maxPlatformVolume_ = platformLimit;
        if (maxPlatformVolume_) {
            if (*maxPlatformVolume_ > maxVolume()) {
                *maxPlatformVolume_ = maxVolume();
            }
            if (isRunning_ && *maxPlatformVolume_ < volumeToSet_) {
                scheduleSetVolumeUnlocked(*maxPlatformVolume_, isMuted_, Animate::DO_NOT_ANIMATE, Tone::DO_NOT_PLAY_TONE, SendAVRCP::SEND_AVRCP, SOURCE_OTHER);
            }
        }
    }
}

// -------------- Notifications messages handling

void VolumeManager::onNotificationPending()
{
    onNotificationStarted();
    sdk_->notifyPreparedForNotification();
}

void VolumeManager::onNotificationStarted()
{
    YIO_LOG_INFO("On notification started");

    if (!isNotificationPlaying_ && !isMuted_) {
        std::lock_guard guard(mutex_);
        stash(currentVolume_);
        setAliceVolumeNotMoreUnlocked(NOTIFICATION_DEFAULT_MAX_VOLUME);
        isNotificationPlaying_ = true;
    }
}

void VolumeManager::onNotificationEnd()
{
    YIO_LOG_INFO("On notification end");

    isNotificationPlaying_ = false;
    std::lock_guard<std::mutex> guard(mutex_);
    if (!isMuted_) {
        unstash();
    }
}

// -------------- Alarm messages handling

void VolumeManager::onAlarmEnqueued(AlarmType alarmType, const std::string& alarmId) {
    YIO_LOG_INFO("On alarm enqueued");

    setAlarmVolume(alarmType);

    sdk_->approveAlarm(alarmId);
}

void VolumeManager::onAlarmStarted(AlarmType alarmType)
{
    YIO_LOG_INFO("On alarm started. isAlarmVolumeSet_: " << isAlarmVolumeSet_);

    // if alarm approval is turned off, we still want to set proper alarm volume
    if (!isAlarmVolumeSet_) {
        setAlarmVolume(alarmType);
    }
}

void VolumeManager::setAlarmVolume(AlarmType alarmType)
{
    YIO_LOG_INFO("Set alarm volume");

    isAlarmVolumeSet_ = true;
    std::lock_guard<std::mutex> guard(mutex_);
    stash(currentVolume_);
    if (alarmType != AlarmType::TIMER) {
        startRaisingVolume(alarmsSettings_.startVolume,
                           alarmsSettings_.finishVolume,
                           std::chrono::milliseconds(alarmsSettings_.volumeRaiseStepMs));
    } else {
        // TODO: вынести 5 куда-то в настройки
        setAliceVolumeNotLessUnlocked(5);
    }
}

void VolumeManager::onAlarmStopped(AlarmType alarmType, bool hasRemainingMedia) {
    YIO_LOG_INFO("On alarm stopped. "
                 << " alarm type: " << static_cast<int>(alarmType) << " hasRemainingMedia: " << hasRemainingMedia);

    if (alarmType == AlarmType::TIMER) {
        std::lock_guard<std::mutex> guard(mutex_);
        unstash();
    } else {
        StopMode mode = StopMode::UNSTASH;

        // if alarm hasn't stop media, volume should be min(stashedVolume, currentVolume)
        if (hasRemainingMedia) {
            std::lock_guard guard(mutex_);
            if (stash_.volume > currentVolume_) {
                // we shouldn't clear stash until alarm remaining media is stopped
                mode = StopMode::DO_NOTHING;
            }
        }

        stopRaisingVolume(mode);
    }
    isAlarmVolumeSet_ = false;
}

void VolumeManager::onAlarmStopRemainingMedia() {
    YIO_LOG_INFO("On alarm stop remaining media");

    std::lock_guard<std::mutex> guard(mutex_);
    unstash();
}

void VolumeManager::onAlarmsSettingsChanged(const AlarmsSettings& alarmsSettings) {
    std::lock_guard<std::mutex> guard(mutex_);
    alarmsSettings_ = alarmsSettings;
    YIO_LOG_INFO("onAlarmsSettingsChanged"
                 << ": start Alice volume -- " << alarmsSettings_.startVolume
                 << ", finish Alice volume -- " << alarmsSettings_.finishVolume
                 << ", Alice volume raise step -- " << alarmsSettings_.volumeRaiseStepMs << " ms");
}

void VolumeManager::sendVolumeManagerStateUnlocked()
{
    quasar::proto::QuasarMessage message;
    message.mutable_volume_manager_message()->mutable_state()->set_platform_volume(currentVolume_);
    message.mutable_volume_manager_message()->mutable_state()->set_alice_volume(scaleToAlice(currentVolume_));
    message.mutable_volume_manager_message()->mutable_state()->set_is_muted(isMuted_);
    message.mutable_volume_manager_message()->mutable_state()->set_source(TString(source_));
    message.mutable_volume_manager_message()->mutable_state()->set_set_bt_volume(sendAVRCP_ == SendAVRCP::SEND_AVRCP);
    volumeManagerServer_->sendToAll(std::move(message));
}

void VolumeManager::setPlatformVolume(int platformVolume, bool isMuted, const std::string& source)
{
    stopRaisingVolume();
    scheduleSetVolume(platformVolume, isMuted, Animate::ANIMATE, Tone::PLAY_TONE, SendAVRCP::SEND_AVRCP, source);
}

void VolumeManager::setAliceVolume(int aliceVolume, bool isMuted, const std::string& source)
{
    setPlatformVolume(scaleFromAlice(aliceVolume), isMuted, source);
}

// -------------- AliceVolumeSetter messages handling

void VolumeManager::volumeDown(const std::string& source) {
    stopRaisingVolume();
    std::lock_guard<std::mutex> guard(mutex_);
    int currentAliceVolume = scaleToAlice(currentVolume_);
    if (currentAliceVolume > 0) {
        --currentAliceVolume;
    }
    int volumeToSet = scaleFromAlice(currentAliceVolume);
    scheduleSetVolumeUnlocked(volumeToSet, false, Animate::ANIMATE, Tone::PLAY_TONE, SendAVRCP::SEND_AVRCP, source);
}

void VolumeManager::volumeUp(const std::string& source) {
    stopRaisingVolume();
    std::lock_guard<std::mutex> guard(mutex_);
    int currentAliceVolume = scaleToAlice(currentVolume_);
    if (currentAliceVolume < MAX_ALICE_VOLUME) {
        ++currentAliceVolume;
    }
    int volumeToSet = scaleFromAlice(currentAliceVolume);
    scheduleSetVolumeUnlocked(volumeToSet, false, Animate::ANIMATE, Tone::PLAY_TONE, SendAVRCP::SEND_AVRCP, source);
}

void VolumeManager::setVolume(int aliceVolume, bool animate, const std::string& source) {
    stopRaisingVolume();
    scheduleSetVolume(scaleFromAlice(aliceVolume), false,
                      animate ? Animate::ANIMATE : Animate::DO_NOT_ANIMATE,
                      source != SOURCE_OTHER ? Tone::PLAY_TONE : Tone::DO_NOT_PLAY_TONE,
                      SendAVRCP::SEND_AVRCP, source);
}

void VolumeManager::mute() {
    stopRaisingVolume();
    std::lock_guard<std::mutex> guard(mutex_);
    muteUnlocked(SOURCE_VOICE);
}

void VolumeManager::unmute() {
    stopRaisingVolume();
    std::lock_guard<std::mutex> guard(mutex_);
    unmuteUnlocked(SOURCE_VOICE);
}

// -------------- Bluetooth messages handling

void VolumeManager::onChangeVolumeAVRCP(int volumePercent) {
    onChangeVolumeAVRCPInternal(volumePercent, SOURCE_BLUETOOTH);
}

void VolumeManager::onChangeVolumeAVRCPInternal(int volumePercent, const std::string& source) {
    Y_VERIFY(volumePercent >= 0 && volumePercent <= 100);
    stopRaisingVolume();
    /* Do not send avrcp event in order to avoid loop btw Source and this device (they will send volume avrcp events until
     * set up value won't be synchronized. So since there always be race condition it will cause issues)
     */
    scheduleSetVolume(std::round(volumePercent / 100.0 * maxVolume()), false, Animate::ANIMATE, Tone::PLAY_TONE, SendAVRCP::DO_NOT_SEND_AVRCP, source, false);
}

// -------------- DeviceState messages handling

std::weak_ptr<YandexIO::IBluetoothStateListener> VolumeManager::getBluetoothStateListener() const {
    return bluetoothStateListener_;
}

void VolumeManager::onSDKState(const YandexIO::SDKState& state) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (isReminderPlaying_ != state.isReminderPlaying) {
        isReminderPlaying_ = state.isReminderPlaying;
        if (isReminderPlaying_) {
            YIO_LOG_INFO("Reminder playing");
            lock.unlock();
            // In case of playing reminder at the same time as an alarm
            raisingVolumeExecutor_.reset(nullptr);
            lock.lock();
            stash(currentVolume_);
            setAliceVolumeNotLessUnlocked(minReminderAliceVolume_);
        } else {
            YIO_LOG_INFO("Reminder not playing");
            unstash();
        }
    }

    const bool newIsMediaPlaying = isMediaPlaying(state);
    if (isScreensaverOn_ != state.screenState.isScreensaverOn || newIsMediaPlaying != isMediaPlaying_) {
        processMediaStateOnScreenSaver(state.screenState.isScreensaverOn, newIsMediaPlaying);
        isScreensaverOn_ = state.screenState.isScreensaverOn;
        isMediaPlaying_ = newIsMediaPlaying;
    }
}

// If we enter screensaver mode and we don't play media we set volume at most 5.
// This prevents case when user sets too loud volume and forgot about it
void VolumeManager::processMediaStateOnScreenSaver(bool newScreenSaverOn, bool newIsMediaPlaying) {
    if (!(isScreensaverOn_ && !isMediaPlaying_) && (newScreenSaverOn && !newIsMediaPlaying)) {
        try {
            // delay volume decreasing because thinmusic send FINISHED between switching tracks
            screenSaverDecreaseVolumeTimer_.start(std::chrono::seconds(15));
        } catch (const std::runtime_error& ex) {
            YIO_LOG_WARN("Error when screenSaverDecreaseVolumeTimer.start(): " << ex.what());
        }
    } else if (newIsMediaPlaying) {
        try {
            screenSaverDecreaseVolumeTimer_.stop();
        } catch (const std::runtime_error& ex) {
            YIO_LOG_WARN("Error when screenSaverDecreaseVolumeTimer.stop(): " << ex.what());
        }
    }
}

bool VolumeManager::isMediaPlaying(const YandexIO::SDKState& state) const {
    return state.playerState.audio.isPlaying || state.playerState.music.isPlaying || state.playerState.radio.isPlaying || isBluetoothPlaying_;
}

// -------------- Volume stash handling

void VolumeManager::stash(int volume) {
    YIO_LOG_INFO("Stash volume " << volume);
    stash_.volume = volume;
    stash_.isMuted = isMuted_;
    stash_.isStashed = true;
}

void VolumeManager::unstash() {
    if (!stash_.isStashed) {
        YIO_LOG_INFO("Nothing is stashed");
        return;
    }
    YIO_LOG_INFO("Unstash volume: " << stash_.volume);
    scheduleSetVolumeUnlocked(stash_.volume, stash_.isMuted, Animate::DO_NOT_ANIMATE, Tone::DO_NOT_PLAY_TONE, SendAVRCP::SEND_AVRCP, SOURCE_OTHER);
    stash_.isStashed = false;
}

void VolumeManager::clearStash() {
    YIO_LOG_INFO("Clear stash");
    stash_.isStashed = false;
}

// -------------- Raising volume loop implementations

void VolumeManager::startRaisingVolume(int /* fromAlice */, int toAlice, std::chrono::milliseconds period) {
    int step = alarmRaisingPlatformVolumeStep();
    auto raisingFunction = [this, toAlice, step] {
        std::lock_guard<std::mutex> guard(mutex_);
        if (currentVolume_ < scaleFromAlice(toAlice)) {
            YIO_LOG_INFO("Raising volume loop: current volume " << currentVolume_ << ", to Alice volume: " << toAlice << ", step: " << step);
            scheduleSetVolumeUnlocked(std::min(currentVolume_ + step, scaleFromAlice(toAlice)),
                                      false,
                                      Animate::DO_NOT_ANIMATE,
                                      Tone::DO_NOT_PLAY_TONE,
                                      SendAVRCP::SEND_AVRCP,
                                      SOURCE_ALARM_RAISING);
        }
    };

    scheduleSetVolumeUnlocked(scaleFromAlice(alarmsSettings_.startVolume), false, Animate::DO_NOT_ANIMATE, Tone::DO_NOT_PLAY_TONE, SendAVRCP::SEND_AVRCP, SOURCE_ALARM_RAISING);

    // E.g. if platform scale has 3 times more divisions and we keep step default we can smoothly increase volume by making period 3 times less
    period *= step;
    period /= (maxVolume() / MAX_ALICE_VOLUME);
    YIO_LOG_INFO("Initializing period volume raising with params: period=" << period.count() << ", step=" << step << ", toAliceVolume=" << toAlice);
    raisingVolumeExecutor_.reset(nullptr);
    raisingVolumeExecutor_ = std::make_unique<PeriodicExecutor>(raisingFunction,
                                                                period,
                                                                PeriodicExecutor::PeriodicType::SLEEP_FIRST);
}

void VolumeManager::stopRaisingVolume(StopMode stopMode) {
    YIO_LOG_INFO("Stop raising volume. stop mode: " << static_cast<int>(stopMode));
    raisingVolumeExecutor_.reset(nullptr);
    std::lock_guard<std::mutex> guard(mutex_);
    if (stopMode == StopMode::UNSTASH) {
        unstash();
    } else if (stopMode == StopMode::CLEAR_STASH) {
        clearStash();
    }
}

// -------------- Storing and loading volume state

void VolumeManager::storeVolumeState(int volume, bool isMuted) {
    PersistentFile volumeFile(currentVolumeFilename_, PersistentFile::Mode::TRUNCATE);
    volumeFile.write(std::to_string(volume));
    YIO_LOG_INFO("Saved volume " << volume);

    PersistentFile muteFile(currentMuteStateFilename_, PersistentFile::Mode::TRUNCATE);
    muteFile.write(isMuted ? "1" : "0");
    YIO_LOG_INFO("Saved isMuted " << isMuted);
}

void VolumeManager::loadVolumeState(int& volume, bool& isMuted) {
    // Default values if files do not exist
    volume = initialVolume();
    isMuted = false;

    std::string line;

    if (fileExists(currentVolumeFilename_)) {
        std::ifstream file(currentVolumeFilename_);
        if (file.good()) {
            std::getline(file, line);
            try {
                volume = std::stoi(line);
                if (volume < 0 || volume > maxVolume()) {
                    YIO_LOG_ERROR_EVENT("VolumeManager.LoadVolumeState.InvalidValue", "Incorrect volume value from file: " << volume << ", setting initial value of " << initialVolume());
                    volume = initialVolume();
                }
                YIO_LOG_INFO("Volume value " << volume << " restored from file");
            } catch (const std::logic_error& e) {
                YIO_LOG_ERROR_EVENT("VolumeManager.LoadVolumeState.Exception", "Error when reading number from file " << currentVolumeFilename_);
            }
        }
    }

    if (fileExists(currentMuteStateFilename_)) {
        std::ifstream file(currentMuteStateFilename_);
        if (file.good()) {
            std::getline(file, line);
            try {
                int muted = std::stoi(line);
                // Here convert int value, stored in file to bool
                isMuted = muted;
                YIO_LOG_INFO("Mute state " << muted << " restored from file");
            } catch (const std::logic_error& e) {
                YIO_LOG_ERROR_EVENT("VolumeManager.LoadMuteState.Exception", "Error when reading number from file " << currentMuteStateFilename_);
            }
        }
    }
}

// -------------- Unlocked implementations

void VolumeManager::muteUnlocked(const std::string& source) {
    scheduleSetVolumeUnlocked(currentVolume_, true, Animate::ANIMATE, Tone::PLAY_TONE, SendAVRCP::SEND_AVRCP, source);
}

void VolumeManager::unmuteUnlocked(const std::string& source) {
    // If we have small volume value when unmute, we set it at least 1 in Alice scale. If we don't do that,
    //  there can be case when we unmute but speaker is still quiet or even have zero volume.
    // TODO: think about minimum unmute volume as as config
    int volumeToSet = currentVolume_ < scaleFromAlice(1) ? scaleFromAlice(1) : currentVolume_;
    scheduleSetVolumeUnlocked(volumeToSet, false, Animate::ANIMATE, Tone::PLAY_TONE, SendAVRCP::SEND_AVRCP, source);
}

void VolumeManager::setAliceVolumeNotLessUnlocked(int aliceVolume) {
    int currentAliceVolume = scaleToAlice(currentVolume_);
    int volumeToSet = currentVolume_;
    if (currentAliceVolume < aliceVolume) {
        volumeToSet = scaleFromAlice(aliceVolume);
    }
    scheduleSetVolumeUnlocked(volumeToSet, false, Animate::DO_NOT_ANIMATE, Tone::DO_NOT_PLAY_TONE, SendAVRCP::SEND_AVRCP, SOURCE_OTHER);
}

void VolumeManager::setAliceVolumeNotMoreUnlocked(int aliceVolume) {
    int currentAliceVolume = scaleToAlice(currentVolume_);
    int volumeToSet = currentVolume_;
    if (currentAliceVolume > aliceVolume) {
        volumeToSet = scaleFromAlice(aliceVolume);
    }
    scheduleSetVolumeUnlocked(volumeToSet, false, Animate::DO_NOT_ANIMATE, Tone::DO_NOT_PLAY_TONE, SendAVRCP::SEND_AVRCP, SOURCE_OTHER);
}

void VolumeManager::setVolumeUnlockedAction(int volume, bool muted, VolumeManager::Animate animate,
                                            VolumeManager::Tone tone, VolumeManager::SendAVRCP sendAVRCP, const std::string& source, bool changeIfSame) {
    if (!changeIfSame && volume == currentVolume_ && muted == isMuted_) {
        return;
    }

    if (stashedVolumeMode_) {
        stash(volume);
        return;
    }

    // Set volume and mute state
    int oldVolume = std::exchange(currentVolume_, volume);
    isMuted_ = muted;
    sendAVRCP_ = sendAVRCP;
    source_ = source;

    // Write info about volume
    storeVolumeState(currentVolume_, isMuted_);

    // Set volume
    setVolumeImplementation(isMuted_ ? 0 : currentVolume_);

    // Animate LEDs
    if (animate == Animate::ANIMATE) {
        playVolumeLed(currentVolume_, isMuted_, source);
    }
    /* Play volume tones if Media isn't playing in that moment */
    if ((tone == Tone::PLAY_TONE) && !isMediaPlaying_) {
        playVolumeTone(oldVolume, currentVolume_);
    }
    /* Do custom callback */
    for (auto it = listeners_.begin(); it != listeners_.end();) {
        if (auto listener = it->lock()) {
            ++it;
            listener->onVolumeChange(currentVolume_, scaleToAlice(currentVolume_), isMuted_, source, sendAVRCP == SendAVRCP::SEND_AVRCP);
        } else {
            it = listeners_.erase(it);
        }
    }

    // Give all needed information about volume to IO SDK
    /* Notify Alice about volume change, so user will be able to ask "Alice, current volume" */
    const int currentAliceVolume = scaleToAlice(currentVolume_);
    const int maxAliceVolume = scaleToAlice(maxVolume());

    sdk_->getDeviceStateCapability()->setVolumeState(isMuted_, currentAliceVolume, maxAliceVolume);

    // Send new state to interface
    sendVolumeManagerStateUnlocked();
}

void VolumeManager::scheduleSetVolume(int volume, bool muted, Animate animate, Tone tone, SendAVRCP sendAVRCP,
                                      const std::string& source, bool changeIfSame)
{
    std::scoped_lock lock(mutex_);
    scheduleSetVolumeUnlocked(volume, muted, animate, tone, sendAVRCP, source, changeIfSame);
}

void VolumeManager::scheduleSetVolumeUnlocked(int volume, bool muted, Animate animate, Tone tone, SendAVRCP sendAVRCP,
                                              const std::string& source, bool changeIfSame)
{
    if (!isRunning_) {
        YIO_LOG_WARN("Volume manager is not started yet");
        return;
    }

    // Check borders
    volume = std::clamp(volume, 0, maxPlatformVolume_ ? *maxPlatformVolume_ : maxVolume());

    YIO_LOG_INFO("Setting volume:"
                 << " value=" << volume
                 << (muted ? ", muted" : ", not muted")
                 << (animate == Animate::ANIMATE ? ", animate" : ", do not animate")
                 << (tone == Tone::PLAY_TONE ? ", play tone" : ", do not play tone")
                 << (sendAVRCP == SendAVRCP::SEND_AVRCP ? ", send AVRCP" : ", do not send AVRCP")
                 << ", source=" << source
                 << (changeIfSame ? ", change if same value" : "")
                 << (stashedVolumeMode_ ? ", stash volume mode" : ""));

    volumeChanged_ = true;
    volumeToSet_ = volume;
    muteStateToSet_ = muted;
    animateToSet_ = animate;
    toneToSet_ = tone;
    sendAVRCPToSet_ = sendAVRCP;
    sourceToSet_ = source;

    if (!blockScheduleVolumeCallback_) {
        blockScheduleVolumeCallback_ = true;
        scheduleCallbackUnlock(
            [this]() {
                std::scoped_lock lock(mutex_);
                setVolumeUnlockedAction(volumeToSet_, muteStateToSet_, animateToSet_, toneToSet_, sendAVRCPToSet_, sourceToSet_);
                blockScheduleVolumeCallback_ = false;
            },
            periodOfSetting_);
    }
}

void VolumeManager::scheduleCallbackUnlock(std::function<void()> callback, std::chrono::milliseconds delayAfterCallback) {
    callbackQueue_.push(CallbackItem{.callback = std::move(callback), .delayAfterCallback = delayAfterCallback});
    CV_.notify_one();
}

void VolumeManager::volumeSettingThread() {
    setThreadSchedulingParams(SCHED_RR, 99);
    std::unique_lock<std::mutex> lock(mutex_);
    while (isRunning_) {
        Y_VERIFY(!callbackQueue_.empty());
        const auto callback = callbackQueue_.front();
        callbackQueue_.pop();

        lock.unlock();

        callback.callback();

        lock.lock();

        CV_.wait_for(lock, callback.delayAfterCallback, [this] {
            return !isRunning_;
        });

        CV_.wait(lock, [this] {
            return !isRunning_ || !callbackQueue_.empty();
        });
    }
}
