#pragma once

#include "configuring_substate.h"
#include "draw_led_screen_directive_observer.h"
#include "led_devices_impl.h"
#include "led_directive_observer.h"
#include "spectrogram_animation.h"
#include "listening/no_reset_listening_pattern.h"
#include "smart_devices/libs/iot/capability/interface/i_iot_capability_events.h"
#include "smart_devices/platforms/yandexstation_2/platformd/leds/clock/clock_animation.h"
#include "smart_devices/platforms/yandexstation_2/platformd/leds/volume/rcu_volume_conductor.h"
#include "smart_devices/platforms/yandexstation_2/platformd/leds/volume/volume_conductor.h"
#include "smart_devices/platforms/yandexstation_2/platformd/foreground/foreground_activity.h"
#include "update/update_animation_conductor.h"

#include <yandex_io/modules/device_state/extended_device_state.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/idle_animation_composition.h>
#include <yandex_io/modules/leds/led_patterns/led_pattern.h>
#include <yandex_io/modules/leds/led_patterns/progress_pattern.h>
#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/device_mode_observer.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/sdk_state.h>
#include <yandex_io/sdk/sdk_state_observer.h>
#include <yandex_io/sdk/spectrum_observer.h>
#include <yandex_io/modules/bluetooth/bluetooth_state_listener/callback_bluetooth_state_listener.h>
#include <yandex_io/modules/bluetooth/bluetooth_state_listener/i_bluetooth_state_listener.h>

#include <memory>

class DeviceStateHandler final: public YandexIO::SDKStateObserver,
                                public YandexIO::DeviceModeObserver,
                                public YandexIO::SpectrumObserver,
                                public YandexIO::BackendConfigObserver,
                                public ClockDisplayStateHolder::Listener,
                                public YandexIO::IIotCapabilityEvents {
public:
    DeviceStateHandler(const Json::Value& maindConfig,
                       std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
                       std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices,
                       std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenDirectiveObserver,
                       std::shared_ptr<LedDirectiveObserver> ledDirectiveObserver,
                       std::shared_ptr<ForegroundActivity> foregroundActivity,
                       std::shared_ptr<YandexIO::SDKInterface> sdk);
    ~DeviceStateHandler() noexcept override;

    void subscribeToSystemConfig();

    void onSDKState(const YandexIO::SDKState& state) override;

    void startBootAnimation();

    void onSoundDataTransferStart() override;
    void onConnectingToNetwork() override;
    void onSetupError() override;

    void onStartSetup(bool /* isFirstSetup */) override {
    }
    void onFinishSetup() override {
    }
    void onConfigureSuccessUpdate(DeviceModeObserver::ConfigurationSuccessState /* configurationSuccessState */) override {
    }

    void onSpectrum(int64_t playerInstanceId, const SpectrumFrame& frame, SpectrumType type) override;
    void onSpectrumStart(int64_t playerInstanceId, SpectrumType type) override;
    void onSpectrumPause(int64_t playerInstanceId, SpectrumType type) override;
    void onSpectrumResume(int64_t playerInstanceId, SpectrumType type) override;

    void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;
    void onDeviceConfig(const std::string& configName, const std::string& jsonConfigValue) override;

    void onClockDisplayStateChanged(YandexIO::ClockDisplayState newState) override;

    /* Helper function to update DOA */
    void setDoaAngle(double doa);

    std::shared_ptr<VolumeConductor> getVolumeConductor();
    std::shared_ptr<RcuVolumeConductor> getRcuVolumeConductor();
    std::weak_ptr<YandexIO::IBluetoothStateListener> getBluetoothStateListener() const;

private:
    void onIotCapabilityDiscoveryStarted() override;
    void onIotCapabilityDiscoveryStopped() override;
    void onIotCapabilityWifiCredentialsReceived() override;
    void onIotCapabilityOff() override;

    /**
     * @brief Draw LED Animation using input DeviceState
     * @param state - input device state that is used to compute led animation to draw
     * @note Should be protected with deviceStateMutex_ (because it use deviceState_);
     */
    void animateDeviceState(const YandexIO::ExtendedDeviceState& state, const YandexIO::SDKState::ConfigurationState& prevConfigurationState, ConfiguringSubstate configuringSubstate);

    /**
     * @brief Decide which background pattern to play using DeviceState
     * @ NOTE: Handle Alarm,Timer, Bluetooth, Alice, Update states
     */
    std::optional<YandexIO::ExtendedDeviceState::SuggestedLedAnimation>
    calculatePattern(const YandexIO::ExtendedDeviceState& state, const YandexIO::SDKState::ConfigurationState& prevConfigurationState, bool isDirectiveAnimationPlaying, bool tillEndOfSpeech);

    void setBluetoothConnectedState(bool state);
    void setBluetoothPlayingState(bool state);
    void playPattern(const YandexIO::ExtendedDeviceState::SuggestedLedAnimation& ledAnimation, std::shared_ptr<Animation> pattern, bool forcePlay = false);

    void playConductor(const YandexIO::ExtendedDeviceState::SuggestedLedAnimation& ledAnimation,
                       std::shared_ptr<AnimationConductor> conductor, bool forcePlay = false);

    void playAnimation(const YandexIO::ExtendedDeviceState& state, const YandexIO::ExtendedDeviceState::SuggestedLedAnimation& ledAnimation,
                       ConfiguringSubstate configuringSubstate);

    std::shared_ptr<AnimationConductor> prepareIdleAnimation(const YandexIO::SDKState& state, const YandexIO::ExtendedDeviceState::SuggestedLedAnimation& ledAnimation, bool skipEmpty = false);
    static std::optional<std::chrono::system_clock::time_point> showTimer(const std::vector<YandexIO::SDKState::TimerState>& timers);

    void playIdleAnimation(const YandexIO::SDKState& state, const YandexIO::ExtendedDeviceState::SuggestedLedAnimation& ledAnimation);
    void processDeviceState(const YandexIO::SDKState& state);

    void processDeviceConfig(const std::string& configName, const std::string& jsonConfigValue);
    void processSystemConfig(const std::string& configName, const std::string& jsonConfigValue);

    std::shared_ptr<YandexIO::SDKInterface> sdk_;
    YandexIO::ClockDisplayState clockDisplayState_;
    bool clockDisplayStateChanged_{false};

    std::atomic<double> doa_{0};
    YandexIO::ExtendedDeviceState deviceState_;
    YandexIO::SDKState::AliceState::State prevAliceState_{YandexIO::SDKState::AliceState::State::NONE};
    bool isBoot_ = true; // BOOT is special case of either unknown device state or UNKNOWN configuration state
    ConfiguringSubstate configuringSubstate_ = ConfiguringSubstate::CONFIGURING;

    YandexIO::ExtendedDeviceState::SuggestedLedAnimation lastSuggestedAnimation_{YandexIO::ExtendedDeviceState::SuggestedLedAnimation::IDLE};

    bool clockSynchronizedOnce_ = false;

    std::chrono::milliseconds emptyAnimationLength_ = std::chrono::seconds(5);

    std::shared_ptr<NoResetListeningPattern> listeningPattern_;
    std::shared_ptr<UpdateAnimationConductor> updateAnimationConductor_;
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<LedDevicesImpl> ledDevices_;
    std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenDirectiveObserver_;
    std::shared_ptr<LedDirectiveObserver> ledDirectiveObserver_;
    std::shared_ptr<DefaultAnimationConductor> activeDrawLedScreenConductor_;
    std::shared_ptr<ForegroundActivity> foregroundActivity_;
    std::shared_ptr<YandexIO::CallbackBluetoothStateListener> bluetoothStateListener_;
    std::string aliceGifLedAnimations_;
    std::string aliceGifLedTemplates_;
    std::optional<YandexIO::SDKState::IotState> iotStateOverride_;

    quasar::Lifetime lifetime_;

    std::string aliceFrontalAnimationFontsPath_;
    std::string bootFlagFile_;

    std::shared_ptr<ClockAnimation> clockAnimation_;

    std::shared_ptr<SpectrogramAnimation> spectrogramAnimation_;
    int64_t spectrogramPlayerInstanceId_{0};
    bool isSpectrogramAnimationEnabled_ = false;

    std::shared_ptr<VolumeConductor> volumeConductor_;
    std::shared_ptr<RcuVolumeConductor> rcuVolumeConductor_;

    quasar::NamedCallbackQueue cbQueue_{"synchronizationSDKStateHandlerQueue"};

    static const int DEFAULT_BOOT_LED_RING_BRIGHTNESS;
    static const int DEFAULT_DIM_LED_RING_BRIGHTNESS;
    static const int DEFAULT_LED_RING_BRIGHTNESS;

    int dimLedRingBrightness_{DEFAULT_DIM_LED_RING_BRIGHTNESS};
    int ledRingBrightness_{DEFAULT_LED_RING_BRIGHTNESS};

    const std::set<YandexIO::ExtendedDeviceState::SuggestedLedAnimation> SKIP_EMPTY_STATES{
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::BLUETOOTH_IS_PLAYING,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_AVAILABLE,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_PASSIVE};

    const std::set<YandexIO::ExtendedDeviceState::SuggestedLedAnimation> DIALOG_STATES{
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::LISTENING,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::THINKING,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::SPEAKING};

    const std::set<YandexIO::ExtendedDeviceState::SuggestedLedAnimation> EXTENDED_DIALOG_STATES{
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::LISTENING,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::IDLE,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::THINKING,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::SPEAKING,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::AUDIO_PLAYING,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::TIMER_COUNTDOWN,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::MULTIROOM_CLOCK_IS_SYNCHRONIZING1,
        YandexIO::ExtendedDeviceState::SuggestedLedAnimation::MULTIROOM_CLOCK_IS_SYNCHRONIZING2};
};
