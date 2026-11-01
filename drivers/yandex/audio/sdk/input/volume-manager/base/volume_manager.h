#pragma once

#include "i_volume_manager_listener.h"

#include <yandex_io/modules/bluetooth/bluetooth_observer/bluetooth_observer.h>
#include <yandex_io/modules/bluetooth/bluetooth_state_listener/callback_bluetooth_state_listener.h>

#include <yandex_io/libs/base/linux_timer.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/threading/periodic_executor.h>

#include <yandex_io/sdk/alarm_observer.h>
#include <yandex_io/sdk/alice_volume_setter.h>
#include <yandex_io/sdk/notification_observer.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/sdk_state.h>
#include <yandex_io/sdk/sdk_state_observer.h>

#include <list>
#include <memory>
#include <optional>
#include <thread>

/**
 * @brief this class implements default volume behaviour on our speakers
 */
class VolumeManager
    : public std::enable_shared_from_this<VolumeManager>,
      public YandexIO::AlarmObserver,
      public YandexIO::AliceVolumeSetter,
      public YandexIO::BluetoothObserver,
      public YandexIO::SDKStateObserver,
      public YandexIO::NotificationObserver {
public:
    VolumeManager(std::shared_ptr<YandexIO::IDevice> device,
                  std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
                  std::shared_ptr<YandexIO::SDKInterface> sdk,
                  std::string currentVolumeFilename = "",
                  std::string currentMuteStateFilename = "",
                  std::chrono::milliseconds periodOfSetting = std::chrono::milliseconds(0));
    ~VolumeManager();

    static constexpr const char* SOURCE_VOICE = "voice";
    static constexpr const char* SOURCE_BUTTON = "button";
    static constexpr const char* SOURCE_BLUETOOTH = "bluetooth";
    static constexpr const char* SOURCE_ALARM_RAISING = "alarm_raising";
    static constexpr const char* SOURCE_OTHER = "other";

    /**
     * Adds listener. If already started, executes first callback immediately.
     * @param listener listener
     */
    void addListener(std::shared_ptr<IVolumeManagerListener> listener);

    /**
     * Starts volume setter: allows manual volume change and registers listeners to IO SDK.
     */
    void start();

    /**
     * Manually set volume, for example from tv source
     * @param volume -- current volume in Alice terms
     * @param source -- source of signal
     */
    void manualSetVolume(int volume, const std::string& source = SOURCE_BUTTON);
    /**
     * Makes volume 1 platform unit louder.
     * @param source -- source of a signal
     */
    void manualVolumeUp(const std::string& source = SOURCE_BUTTON);
    /**
     * Makes volume 1 platform unit quieter.
     * @param source -- source of a signal
     */
    void manualVolumeDown(const std::string& source = SOURCE_BUTTON);
    /**
     * Changes platform volume by diff platforms units.
     * @param diff -- relative difference (can be negative).
     * @param source -- source of a signal
     */
    void manualVolumeChange(int diff, const std::string& source = SOURCE_BUTTON);
    /**
     * Mutes volume.
     * @param source -- source of a signal
     */
    void manualMute(const std::string& source = SOURCE_BUTTON);
    /**
     * Unmutes volume.
     * @param source -- source of a signal
     */
    void manualUnmute(const std::string& source = SOURCE_BUTTON);
    /**
     * Gets current platform volume.
     * @return current platform volume.
     */
    int manualGetCurrentVolume();
    /**
     * Get current muted status
     * @return is muted as known
     */
    bool manualGetIsMuted();

    /**
     * Set maximum volume in Alice scale
     * @param limit max volume, std::nullopt if limitless
     */
    void setAliceVolumeMaxLimit(std::optional<int> aliceLimit);

    /**
     * Function which converts Alice volume to platform volume.
     * @param aliceVolume -- Alice volume (from 0 to 10).
     * @return platform volume (from 0 to maxVolume()).
     */
    virtual int scaleFromAlice(int aliceVolume) = 0;
    /**
     * Function which converts platform volume to Alice volume.
     * @param platformVolume -- platform volume (from 0 to maxVolume()).
     * @return Alice volume (from 0 to 10).
     */
    virtual int scaleToAlice(int platformVolume) = 0;

    /**
     * Min platform volume.
     * @return min platform volume.
     */
    virtual int minVolume() = 0;
    /**
     * Max platform volume.
     * @return max platform volume.
     */
    virtual int maxVolume() = 0;

    /**
     * getter for BluetoothStateListener
     * @return BluetoothStateListener
     */
    std::weak_ptr<YandexIO::IBluetoothStateListener> getBluetoothStateListener() const;

    // In stash volume mode we don't change volume level immediately. Instead, we save requested value and set it on disable
    void enableStashVolumeMode(int platformVolume);
    void disableStashVolumeMode();

protected:
    /**
     * Volume and mute set implementation. All volume sets finally call this function.
     */
    virtual void setVolumeImplementation(int platformVolume) = 0;

    /**
     * Volume step when changing volume up/down
     * @return platform volume step
     */
    virtual int volumeStep() = 0;
    /**
     * Initial volume which will be set if volume file is absent.
     * @return platform volume.
     */
    virtual int initialVolume() = 0;
    /**
     * Plays volume led. By default it's an empty stub.
     *  If led playing is not needed, leave it empty.
     * @param platformValue -- platform volume (from 0 to maxVolume()).
     * @param mutes -- is sound muted
     * @param source --  source of a signal
     */
    virtual void playVolumeLed(int /* platformValue */, bool /* muted */, const std::string& /* source */){};
    /**
     * Play volume tick. By default it's an empty stub.
     *  If led playing is not needed, leave it empty.
     *  Is not called when media content is playing.
     *  Some input values are provided because some implementations may depend on it (e.g. max volume tick is different,
     *  or not every step is accompanied with tick).
     * @param oldVolume -- platform volume which was before volume set.
     * @param newVolume -- platform volume which is after volume set.
     */
    virtual void playVolumeTone(int /* oldVolume */, int /* newVolume */){};

    /**
     * Used for alarm volume raising policy. Should return volume step while doing alarm raising volume.
     * Raising period is inversely proportional to step, so total raising time will remain the same.
     * **What problem does this method solve**: if platform scale is too wide (e.g. from 0 to 10000) alarm raising volume will occur too fast.
     * So 1) logs will be flooded 2) too many resources will be wasted on waking up thread.
     * Increasing step value will solve this problem.
     * @return volume step (in platform values) used for one raising step. Must be positive!
     */
    virtual int alarmRaisingPlatformVolumeStep() const {
        return 1;
    }

    /* BluetoothObserver implementation */

    /**
     * This method is expected to be overridden by platform implementations if they want to pass specific source for volume event.
     * @param volumePercent -- volume level from AVRCP event payload.
     */
    void onChangeVolumeAVRCP(int volumePercent) override;

    /**
     * Actually set volume and play relevant animations for AVRCP volume events.
     * @param volumePercent -- volume level from AVRCP event payload.
     * @param source -- source of event.
     */
    void onChangeVolumeAVRCPInternal(int volumePercent, const std::string& source);

    constexpr static int MAX_ALICE_VOLUME = 10;

    /* AlarmObserver implementation */
    void onAlarmEnqueued(AlarmType alarmType, const std::string& alarmId) override;
    void onAlarmStarted(AlarmType alarmType) override;
    void onAlarmStopped(AlarmType alarmType, bool hasRemainingMedia) override;
    void onAlarmStopRemainingMedia() override;
    void onAlarmsSettingsChanged(const AlarmsSettings& alarmsSettings) override;

    void sendVolumeManagerStateUnlocked();
    void setPlatformVolume(int platformVolume, bool isMuted, const std::string& source);
    void setAliceVolume(int aliceVolume, bool isMuted, const std::string& source);

private:
    /* AliceVolumeSetter implementation */
    void volumeUp(const std::string& source = YandexIO::AliceVolumeSetter::SOURCE_VOICE) override;
    void volumeDown(const std::string& source = YandexIO::AliceVolumeSetter::SOURCE_VOICE) override;
    void setVolume(int aliceVolume, bool animate, const std::string& source = YandexIO::AliceVolumeSetter::SOURCE_VOICE) override;
    void mute() override;
    void unmute() override;

    /* BluetoothObserver implementation */
    void onDiscoveryStart() override{};
    void onDiscoveryStop() override{};
    void onSourceConnected(const std::string& /* btAddr */) override{};
    void onSourceDisconnected(const std::string& /* btAddr */) override{};
    void onFactoryResetComplete() override{};

    /* SDKStateObserver implementation */
    void onSDKState(const YandexIO::SDKState& state) override;
    bool isMediaPlaying(const YandexIO::SDKState& state) const;

    /* NotificationObserver implementation */
    void onNotificationPending() override;
    void onNotificationStarted() override;
    void onNotificationEnd() override;

    mutable std::mutex mutex_;

    enum class Animate {
        ANIMATE,
        DO_NOT_ANIMATE
    };
    enum class Tone {
        PLAY_TONE,
        DO_NOT_PLAY_TONE
    };
    enum class SendAVRCP {
        SEND_AVRCP,
        DO_NOT_SEND_AVRCP
    };

    /* These functions can be called from different places, so they are put here */
    void muteUnlocked(const std::string& source);
    void unmuteUnlocked(const std::string& source);
    void setAliceVolumeNotLessUnlocked(int aliceVolume);
    void setAliceVolumeNotMoreUnlocked(int aliceVolume);

    void scheduleSetVolume(int volume, bool muted, Animate animate, Tone tone, SendAVRCP sendAVRCP,
                           const std::string& source, bool changeIfSame = true);
    void scheduleSetVolumeUnlocked(int volume, bool muted, Animate animate, Tone tone, SendAVRCP sendAVRCP,
                                   const std::string& source, bool changeIfSame = true);
    void setVolumeUnlockedAction(int volume, bool muted, Animate animate, Tone tone, SendAVRCP sendAVRCP,
                                 const std::string& source, bool changeIfSame = true);

    /* Store and load volume state */
    void loadVolumeState(int& volume, bool& isMuted);
    void storeVolumeState(int volume, bool isMuted);

    void processMediaStateOnScreenSaver(bool newScreenSaverOn, bool newIsMediaPlaying);

    void volumeSettingThread();
    std::thread volumeSettingThread_;
    quasar::SteadyConditionVariable CV_;
    bool volumeChanged_{false};
    int volumeToSet_{0};
    bool muteStateToSet_{false};
    Tone toneToSet_{Tone::DO_NOT_PLAY_TONE};
    Animate animateToSet_{Animate::DO_NOT_ANIMATE};
    SendAVRCP sendAVRCPToSet_{SendAVRCP::DO_NOT_SEND_AVRCP};
    std::string sourceToSet_ = SOURCE_OTHER;

    std::shared_ptr<YandexIO::IDevice> device_;

    const std::string currentVolumeFilename_;
    const std::string currentMuteStateFilename_;
    const std::chrono::milliseconds periodOfSetting_;

    int currentVolume_ = 0;
    bool isMuted_ = false;
    SendAVRCP sendAVRCP_{SendAVRCP::DO_NOT_SEND_AVRCP};
    std::string source_{SOURCE_OTHER};
    static const int NOTIFICATION_DEFAULT_MAX_VOLUME = 5;

    bool stashedVolumeMode_ = false;

    /* Volume stash handling */
    struct Stash {
        int volume;
        bool isMuted;
        bool isStashed{false};
    };
    Stash stash_;
    void stash(int volume);
    void unstash();
    void clearStash();

    bool isRunning_{false};

    bool isBluetoothPlaying_{false};
    bool isMediaPlaying_{false};
    bool isReminderPlaying_{false};
    bool isNotificationPlaying_{false};
    bool isScreensaverOn_{false};

    int minReminderAliceVolume_{1};

    std::unique_ptr<quasar::PeriodicExecutor> raisingVolumeExecutor_;
    YandexIO::AlarmObserver::AlarmsSettings alarmsSettings_;
    bool isAlarmVolumeSet_{false};
    void setAlarmVolume(AlarmType alarmType);
    void startRaisingVolume(int fromAlice, int toAlice, std::chrono::milliseconds period);

    enum class StopMode {
        CLEAR_STASH,
        UNSTASH,
        DO_NOTHING
    };
    void stopRaisingVolume(StopMode stopMode = StopMode::CLEAR_STASH);

    std::list<std::weak_ptr<IVolumeManagerListener>> listeners_;

    std::shared_ptr<IVolumeManagerListener> metricsVolumeListener_;

    std::optional<int> maxPlatformVolume_;
    std::shared_ptr<quasar::ipc::IServer> volumeManagerServer_;

    std::shared_ptr<YandexIO::CallbackBluetoothStateListener> bluetoothStateListener_;

    quasar::LinuxTimer screenSaverDecreaseVolumeTimer_;

    bool blockScheduleVolumeCallback_{false};

    struct CallbackItem {
        std::function<void()> callback;
        std::chrono::milliseconds delayAfterCallback{0};
    };
    std::queue<CallbackItem> callbackQueue_;

    void scheduleCallbackUnlock(std::function<void()> callback, std::chrono::milliseconds delayAfterCallback = std::chrono::milliseconds{0});

protected: // allow child classes to use sdk_
    const std::shared_ptr<YandexIO::SDKInterface> sdk_;
};
