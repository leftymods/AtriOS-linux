#include "compound_animation.h"
#include "device_state_handler.h"
#include "empty_animation.h"
#include "gif_animation.h"
#include "led_devices_impl.h"
#include "repeated_animation.h"
#include "alarm/alarm_animation_conductor.h"
#include "configuring/configuring_animation_conductor.h"
#include "idle/oknyx_out_animation_composition.h"
#include "listening/activation_animation_composition.h"
#include "listening/activation_to_listening_animation_composition.h"
#include "listening/listening_animation_composition.h"
#include "speaking/speaking_animation_composition.h"
#include "speaking/speaking_start_animation_composition.h"
#include "thinking/listening_to_thinking_animation_composition.h"
#include "thinking/thinking_animation_composition.h"
#include "timer/timer_conductor.h"
#include "volume/volume_conductor.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/subprocess/subprocess.h>
#include <yandex_io/modules/leds/led_manager/ng/animation.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <tuple>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;
using YandexIO::ExtendedDeviceState;
using YandexIO::SDKState;

namespace {

    const int MIN_BRIGHTNESS = 0;
    const int MAX_BRIGHTNESS = 255;

    void setBootLedRingBrightness(int brightness) {
        brightness = std::clamp(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
        const auto strBrightness = std::to_string(brightness);
        try {
            const int error = runSystemCommand("/system/bin/fw_setenv", {"led_ring_brightness", strBrightness});
            if (error) {
                YIO_LOG_WARN("Led ring boot brightess was not changed, error code " << error);
            } else {
                YIO_LOG_INFO("Led ring boot brightness was set to " << strBrightness);
            }
        } catch (const std::runtime_error& e) {
            YIO_LOG_WARN("Led ring boot brightess was not changed, error " << e.what())
        }
    }

    void setLedRingBrightness(int brightness) {
        brightness = std::clamp(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
        const auto strBrightness = std::to_string(brightness);
        std::ofstream config{"/sys/class/backlight/fb5/brightness"};
        config << strBrightness;
        YIO_LOG_INFO("Led ring brightness was set to " << strBrightness);
    }
} // namespace

const int DeviceStateHandler::DEFAULT_BOOT_LED_RING_BRIGHTNESS = 10;
const int DeviceStateHandler::DEFAULT_DIM_LED_RING_BRIGHTNESS = 10;
const int DeviceStateHandler::DEFAULT_LED_RING_BRIGHTNESS = 255;

DeviceStateHandler::DeviceStateHandler(const Json::Value& maindConfig,
                                       std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
                                       std::shared_ptr<quasar::LedManager> ledManager,
                                       std::shared_ptr<LedDevicesImpl> ledDevices,
                                       std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenDirectiveObserver,
                                       std::shared_ptr<LedDirectiveObserver> ledDirectiveObserver,
                                       std::shared_ptr<ForegroundActivity> foregroundActivity,
                                       std::shared_ptr<YandexIO::SDKInterface> sdk)
    : sdk_(std::move(sdk))
    , ledManager_(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
    , drawLedScreenDirectiveObserver_(std::move(drawLedScreenDirectiveObserver))
    , ledDirectiveObserver_(std::move(ledDirectiveObserver))
    , foregroundActivity_(std::move(foregroundActivity))
{
    Y_VERIFY(ledManager_.get() != nullptr);
    setBootLedRingBrightness(DEFAULT_BOOT_LED_RING_BRIGHTNESS);

    /* Prepare listening pattern. Only DOA will be changed */
    const bool invertRotation = true;
    listeningPattern_ = std::make_shared<NoResetListeningPattern>(ledManager_->getPattern("activate-alisa.led"), 45.0, invertRotation);

    /* start with idle pattern. Make sure leds are disabled */
    auto pattern = ledManager_->getIdlePattern();
    /* Note: set backgroundOverride to true, so The first played animation will be background. otherwise all background
     * animations will be skipped until first not background animation will be played
     */
    ledManager_->play(std::make_shared<DefaultAnimationConductor>(std::move(pattern)));
    aliceGifLedAnimations_ = ledDevices_->getDefaultAnimationsPath();
    aliceGifLedTemplates_ = quasar::getString(maindConfig, "aliceFrontalTemplatesPath");
    bootFlagFile_ = quasar::getString(maindConfig, "bootFlagFile");
    aliceFrontalAnimationFontsPath_ = quasar::getString(maindConfig, "aliceFrontalAnimationFontsPath");

    /* Prepare UpdateAnimationConductor to be used.
       Creating this and further conductors may throw exception.
       Exceptions in this constructor should bubble up */
    updateAnimationConductor_ = std::make_shared<UpdateAnimationConductor>(
        aliceGifLedAnimations_,
        ledDevices_->getLedScreen(),
        ledManager_);

    spectrogramAnimation_ = std::make_shared<YandexStation2SpectrogramAnimation>(ipcFactory, ledDevices_->getLedScreen());
    drawLedScreenDirectiveObserver_->setEventListener([this]() { onSDKState(deviceState_.sdkState); });

    volumeConductor_ = std::make_shared<VolumeConductor>(aliceGifLedAnimations_, ledManager_, ledDevices_->getLedScreen());
    rcuVolumeConductor_ = std::make_shared<RcuVolumeConductor>(aliceGifLedAnimations_, ledManager_, ledDevices_->getLedScreen());

    clockAnimation_ = std::make_shared<ClockAnimation>(ledDevices_->getLedScreen(), ConfigUtils::getClockFont(Json::Value(), aliceFrontalAnimationFontsPath_));

    foregroundActivity_->isActiveSignal()->connect(
        [this](const bool isActive) {
            std::ignore = isActive;
            onSDKState(deviceState_.sdkState);
        },
        lifetime_);
    bluetoothStateListener_ = std::make_shared<YandexIO::CallbackBluetoothStateListener>([this](bool state) { setBluetoothPlayingState(state); }, [this](bool state) { setBluetoothConnectedState(state); });
    deviceState_.bluetoothStreamInState.emplace();
}

std::weak_ptr<YandexIO::IBluetoothStateListener> DeviceStateHandler::getBluetoothStateListener() const {
    return bluetoothStateListener_;
}

void DeviceStateHandler::onIotCapabilityDiscoveryStarted()
{
    cbQueue_.add([this]() {
        iotStateOverride_ = YandexIO::SDKState::IotState::DISCOVERY_IN_PROGRESS;
        animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, configuringSubstate_);
    });
}

void DeviceStateHandler::onIotCapabilityDiscoveryStopped()
{
    cbQueue_.add([this]() {
        iotStateOverride_ = YandexIO::SDKState::IotState::IDLE;
        animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, configuringSubstate_);
    });
}

void DeviceStateHandler::onIotCapabilityWifiCredentialsReceived()
{
    // do nothing
}

void DeviceStateHandler::onIotCapabilityOff()
{
    cbQueue_.add([this]() {
        iotStateOverride_.reset();
        animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, configuringSubstate_);
    });
}

void DeviceStateHandler::subscribeToSystemConfig() {
    sdk_->subscribeToSystemConfig(ConfigUtils::SPECTROGRAM_ANIMATION_CONFIG);
    sdk_->subscribeToSystemConfig(ConfigUtils::LED_USER_CONFIG);
    sdk_->subscribeToSystemConfig(ConfigUtils::DEVICE_STATE_HANDLER_CONFIG);
}

void DeviceStateHandler::startBootAnimation() {
    if (fileExists(bootFlagFile_)) {
        return;
    }
    setLedRingBrightness(dimLedRingBrightness_);
    std::ofstream out(bootFlagFile_);
    out.write("true", 4);
    YIO_LOG_DEBUG("Playing boot animation ");
    try {
        auto bootComposition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{GifAnimation::readGif(
                aliceGifLedAnimations_ + "boot/25x16_FirstBoot_LoadLoop02.gif", ledDevices_->getLedScreen())},
            true);
        auto bootConductor = std::make_shared<DefaultAnimationConductor>(bootComposition);
        bootConductor->setSubstitutionType(SubstitutionType::BACKGROUND_DEFAULT);
        ledManager_->play(std::move(bootConductor));
        cbQueue_.add([this]() {
            isBoot_ = true;
        });
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("DemoModeFrontLedHandler.StartBootAnimation.Exception", "failed to draw on led screen: " << e.what());
    }
}

void DeviceStateHandler::onClockDisplayStateChanged(YandexIO::ClockDisplayState newState) {
    cbQueue_.add([this, newState]() {
        if (newState.isClockOn == clockDisplayState_.isClockOn) {
            return;
        }

        clockDisplayState_ = newState;
        clockDisplayStateChanged_ = true;
        animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, configuringSubstate_);
    });
}

void DeviceStateHandler::onSDKState(const YandexIO::SDKState& state) {
    cbQueue_.add([this, state]() { processDeviceState(state); });
}

void DeviceStateHandler::setBluetoothConnectedState(bool state) {
    cbQueue_.add([this, state]() {
        deviceState_.bluetoothStreamInState.value().isConnected = state;
        animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, configuringSubstate_);
        ledDirectiveObserver_->updateDeviceState(deviceState_);
    });
}

void DeviceStateHandler::setBluetoothPlayingState(bool state) {
    cbQueue_.add([this, state]() {
        deviceState_.bluetoothStreamInState.value().isPlaying = state;
        animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, configuringSubstate_);
        ledDirectiveObserver_->updateDeviceState(deviceState_);
    });
}

void DeviceStateHandler::processDeviceState(const SDKState& state) {
    if (state.aliceState.state != deviceState_.sdkState.aliceState.state) {
        prevAliceState_ = deviceState_.sdkState.aliceState.state;
    }
    auto prevConfigurationState = deviceState_.sdkState.configurationState;
    deviceState_.sdkState = state;
    animateDeviceState(deviceState_, prevConfigurationState, configuringSubstate_);

    ledDirectiveObserver_->updateDeviceState(deviceState_);
}

std::optional<YandexIO::ExtendedDeviceState::SuggestedLedAnimation> DeviceStateHandler::calculatePattern(const YandexIO::ExtendedDeviceState& state, const YandexIO::SDKState::ConfigurationState& prevConfigurationState, bool isDirectiveAnimationPlaying, bool tillEndOfSpeech) {
    /* Alarms, Timers are top priority */
    if (state.sdkState.isAlarmPlaying) {
        return ExtendedDeviceState::SuggestedLedAnimation::ALARM;
    }
    if (state.sdkState.isTimerPlaying) {
        return ExtendedDeviceState::SuggestedLedAnimation::TIMER;
    }

    if (state.sdkState.updateState.isCritical) {
        switch (state.sdkState.updateState.state) {
            case SDKState::UpdateState::State::DOWNLOADING:
                return ExtendedDeviceState::SuggestedLedAnimation::UPDATE_DOWNLOADING;
            case SDKState::UpdateState::State::APPLYING:
                return ExtendedDeviceState::SuggestedLedAnimation::UPDATE_APPLYING;
            case SDKState::UpdateState::State::NONE:
                break;
        }
    }

    if (foregroundActivity_->isActive()) {
        return ExtendedDeviceState::SuggestedLedAnimation::FOREGROUND_ACTIVITY;
    }

    switch (state.sdkState.aliceState.state) {
        case SDKState::AliceState::State::SPEAKING:
            return ExtendedDeviceState::SuggestedLedAnimation::SPEAKING;
        case SDKState::AliceState::State::LISTENING:
            return ExtendedDeviceState::SuggestedLedAnimation::LISTENING;
        case SDKState::AliceState::State::THINKING:
            if (isDirectiveAnimationPlaying) {
                YIO_LOG_INFO("start playing when otherwise THINKING animation should be used");
                return ExtendedDeviceState::SuggestedLedAnimation::SPEAKING;
            }
            return ExtendedDeviceState::SuggestedLedAnimation::THINKING;
        default:
            break;
    }
    /* Handle Configuration State. NOTE: R2D2 and wifi connecting are handled separatly */
    switch (state.sdkState.configurationState) {
        case SDKState::ConfigurationState::CONFIGURING:
            return ExtendedDeviceState::SuggestedLedAnimation::CONFIGURING;
        case SDKState::ConfigurationState::CONFIGURED:
            if (prevConfigurationState != SDKState::ConfigurationState::CONFIGURED) {
                return ExtendedDeviceState::SuggestedLedAnimation::CONFIGURED;
            }
            break;
        case SDKState::ConfigurationState::UNKNOWN:
            return std::nullopt;
        default:
            break;
    }
    if (state.sdkState.notificationState == SDKState::NotificationState::AVAILABLE) {
        return ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_AVAILABLE;
    }
    const auto iotState = iotStateOverride_.value_or(state.sdkState.iotState);
    switch (iotState) {
        case SDKState::IotState::STARTING_DISCOVERY:
        case SDKState::IotState::DISCOVERY_IN_PROGRESS:
            return ExtendedDeviceState::SuggestedLedAnimation::IOT_DISCOVERY_IN_PROGRESS;
        default:
            break;
    }

    switch (state.sdkState.callState.status) {
        case SDKState::CallState::Status::NEW:
        case SDKState::CallState::Status::DIALING:
        case SDKState::CallState::Status::RINGING:
        case SDKState::CallState::Status::ACCEPTING:
            switch (state.sdkState.callState.direction) {
                case SDKState::CallState::Direction::INCOMING:
                    return ExtendedDeviceState::SuggestedLedAnimation::CALL_RINGING_INCOMING;
                case SDKState::CallState::Direction::OUTGOING:
                    return ExtendedDeviceState::SuggestedLedAnimation::CALL_RINGING_OUTGOING;
            }
        case SDKState::CallState::Status::CONNECTING:
            return ExtendedDeviceState::SuggestedLedAnimation::CALL_CONNECTING;
        case SDKState::CallState::Status::CONNECTED:
            return ExtendedDeviceState::SuggestedLedAnimation::CALL_CONNECTED;
        case SDKState::CallState::Status::ENDED_OK:
            return ExtendedDeviceState::SuggestedLedAnimation::CALL_ENDED_OK;
        case SDKState::CallState::Status::ENDED_FAILURE:
            return ExtendedDeviceState::SuggestedLedAnimation::CALL_ENDED_FAILURE;
        default:
            break;
    }

    if (state.bluetoothStreamInState.value().isPlaying) {
        return ExtendedDeviceState::SuggestedLedAnimation::BLUETOOTH_IS_PLAYING;
    }

    if (isDirectiveAnimationPlaying && !tillEndOfSpeech) {
        YIO_LOG_INFO("continue playing when otherwise idle/music/timer animation should be used");
        return ExtendedDeviceState::SuggestedLedAnimation::SPEAKING;
    }

    // don't use SDKState::isMediaPlaying() now, due to Java radio on station cannot draw spectrogram
    if (state.sdkState.playerState.audio.isPlaying ||
        state.sdkState.playerState.music.isPlaying ||
        state.sdkState.multiroomState.playing) {
        return ExtendedDeviceState::SuggestedLedAnimation::AUDIO_PLAYING;
    }

    if (!state.sdkState.timers.empty() && showTimer(state.sdkState.timers).has_value()) {
        return ExtendedDeviceState::SuggestedLedAnimation::TIMER_COUNTDOWN;
    }

    if (state.sdkState.multiroomState.slaveClockSyncing) {
        if (state.sdkState.multiroomState.slaveSyncLevel == SDKState::MultiroomState::SyncLevel::NONE) {
            return ExtendedDeviceState::SuggestedLedAnimation::MULTIROOM_CLOCK_IS_SYNCHRONIZING1;
        } else if (state.sdkState.multiroomState.slaveSyncLevel == SDKState::MultiroomState::SyncLevel::WEAK) {
            return ExtendedDeviceState::SuggestedLedAnimation::MULTIROOM_CLOCK_IS_SYNCHRONIZING2;
        }
    }

    if (state.sdkState.notificationState == SDKState::NotificationState::PASSIVE) {
        return ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_PASSIVE;
    }

    return ExtendedDeviceState::SuggestedLedAnimation::IDLE; // SDKState::ConfigurationState::CONFIGURED:
}

void DeviceStateHandler::animateDeviceState(const ExtendedDeviceState& state, const YandexIO::SDKState::ConfigurationState& prevConfigurationState, ConfiguringSubstate configuringSubstate)
{
    // calculate "real" animation that should be playing
    auto ledAnimation = calculatePattern(state, prevConfigurationState, false, true);
    if (ledAnimation) {
        bool currentScreenStateAllowsExtendedDirective = EXTENDED_DIALOG_STATES.count(ledAnimation.value()) != 0;
        bool aliceDialogWasActive = DIALOG_STATES.count(lastSuggestedAnimation_) != 0;
        bool newDialogPhaseStarts = lastSuggestedAnimation_ == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::SPEAKING &&
                                    ledAnimation.value() == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::LISTENING;
        if (!currentScreenStateAllowsExtendedDirective ||
            !aliceDialogWasActive || newDialogPhaseStarts)
        {
            YIO_LOG_DEBUG("stopping drawLedScreenDirectiveObserver");
            drawLedScreenDirectiveObserver_->stop();
            activeDrawLedScreenConductor_ = nullptr;
        }
        // if drawLedScreenDirective still wants to play, allow it, but not before other animations
        if (drawLedScreenDirectiveObserver_->isPlaying()) {
            ledAnimation = calculatePattern(state, prevConfigurationState, true, drawLedScreenDirectiveObserver_->tillEndOfSpeech());
        }
        YIO_LOG_DEBUG("Suggested Animation: " << int(ledAnimation.value()) << " isboot = " << isBoot_ << " ConfiguringSubstate " << int(configuringSubstate) << " drawLedScreenDirectiveObserver_->isPlaying() " << drawLedScreenDirectiveObserver_->isPlaying());
        try {
            playAnimation(state, ledAnimation.value(), configuringSubstate);
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("DeviceStateHandler.AnimateDeviceState.Exception", "failed to draw on led screen: " << e.what());
        }
    }
}

void DeviceStateHandler::playAnimation(const ExtendedDeviceState& state, const ExtendedDeviceState::SuggestedLedAnimation& ledAnimation,
                                       ConfiguringSubstate configuringSubstate) {
    if (lastSuggestedAnimation_ != ledAnimation && lastSuggestedAnimation_ == ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_AVAILABLE) {
        ledManager_->stop();
    }
    if (lastSuggestedAnimation_ != ledAnimation && lastSuggestedAnimation_ == ExtendedDeviceState::SuggestedLedAnimation::LISTENING) {
        listeningPattern_->finish();
    }
    if (lastSuggestedAnimation_ != ledAnimation && (ledAnimation == ExtendedDeviceState::SuggestedLedAnimation::UPDATE_DOWNLOADING || ledAnimation == ExtendedDeviceState::SuggestedLedAnimation::UPDATE_APPLYING)) {
        setLedRingBrightness(dimLedRingBrightness_);
    }
    const auto isBoot = isBoot_;
    switch (ledAnimation) {
        case ExtendedDeviceState::SuggestedLedAnimation::FOREGROUND_ACTIVITY: {
            std::shared_ptr<ForegroundAppAnimation> animation = foregroundActivity_->getAnimation();
            const auto conductor = std::make_shared<DefaultAnimationConductor>(animation);
            playConductor(ledAnimation, conductor, false);
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::BLUETOOTH_IS_PLAYING: {
            const bool forcePlay = clockDisplayStateChanged_;
            playConductor(ledAnimation, prepareIdleAnimation(state.sdkState, ledAnimation, true), forcePlay);
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::LISTENING: {
            const auto doa = doa_.load();

            if (lastSuggestedAnimation_ == ledAnimation && !isBoot_) {
                break;
            }
            listeningPattern_->manualResetAnimation();
            listeningPattern_->setDoaAngle(doa);

            if (prevAliceState_ != SDKState::AliceState::State::SPEAKING) {
                // should "activate" oknyx
                auto listeningAnimationConductor = std::make_shared<DefaultAnimationConductor>(
                    std::vector<const std::shared_ptr<AnimationComposition>>{
                        std::make_shared<ActivationAnimationComposition>(listeningPattern_,
                                                                         aliceGifLedAnimations_,
                                                                         ledDevices_->getLedScreen(),
                                                                         ledManager_),
                        std::make_shared<ActivationToListeningAnimationComposition>(listeningPattern_,
                                                                                    aliceGifLedAnimations_,
                                                                                    ledDevices_->getLedScreen(),
                                                                                    ledManager_),
                        std::make_shared<ListeningAnimationComposition>(listeningPattern_,
                                                                        aliceGifLedAnimations_,
                                                                        ledDevices_->getLedScreen(),
                                                                        ledManager_)}, SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<activation_listening>");
                playConductor(ledAnimation, std::move(listeningAnimationConductor));
                lastSuggestedAnimation_ = ledAnimation;
                break;
            }

            // second listening in voice session. So no activation
            auto listeningAnimationConductor = std::make_shared<DefaultAnimationConductor>(
                std::vector<const std::shared_ptr<AnimationComposition>>{
                    std::make_shared<ActivationToListeningAnimationComposition>(listeningPattern_,
                                                                                aliceGifLedAnimations_,
                                                                                ledDevices_->getLedScreen(),
                                                                                ledManager_),
                    std::make_shared<ListeningAnimationComposition>(listeningPattern_,
                                                                    aliceGifLedAnimations_,
                                                                    ledDevices_->getLedScreen(),
                                                                    ledManager_)}, SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<listening>");
            playConductor(ledAnimation, std::move(listeningAnimationConductor));
            lastSuggestedAnimation_ = ledAnimation;
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::SPEAKING: {
            if (drawLedScreenDirectiveObserver_->isPlaying()) {
                auto newDrawLedScreenConductor = drawLedScreenDirectiveObserver_->getActiveConductor();
                if (newDrawLedScreenConductor && newDrawLedScreenConductor != activeDrawLedScreenConductor_) {
                    activeDrawLedScreenConductor_ = std::move(newDrawLedScreenConductor);
                    isBoot_ = false;
                    lastSuggestedAnimation_ = ledAnimation;
                    YIO_LOG_INFO("Conductor to play: " << activeDrawLedScreenConductor_->getName());
                    ledManager_->play(activeDrawLedScreenConductor_);
                } else {
                    YIO_LOG_DEBUG("no new drawLedScreenConductor, doing nothing aka continuing playing old animation");
                }
            } else {
                if (lastSuggestedAnimation_ != ledAnimation || activeDrawLedScreenConductor_ || isBoot_) {
                    auto speakingAnimationConductor = std::make_shared<DefaultAnimationConductor>(
                        std::vector<const std::shared_ptr<AnimationComposition>>{
                            std::make_shared<SpeakingStartAnimationComposition>(aliceGifLedAnimations_,
                                                                                ledDevices_->getLedScreen(),
                                                                                ledManager_),
                            std::make_shared<SpeakingAnimationComposition>(aliceGifLedAnimations_,
                                                                           ledDevices_->getLedScreen(),
                                                                           ledManager_)}, SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<speaking>");
                    activeDrawLedScreenConductor_ = nullptr;
                    isBoot_ = false;
                    lastSuggestedAnimation_ = ledAnimation;
                    YIO_LOG_INFO("Conductor to play: " << speakingAnimationConductor->getName());
                    ledManager_->play(std::move(speakingAnimationConductor));
                    break;
                }
            }
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::THINKING: {
            if (lastSuggestedAnimation_ != ledAnimation || activeDrawLedScreenConductor_ || isBoot_) {
                const auto doa = doa_.load();
                auto thinkingRing = ledManager_->getPattern("thinking.led")->rotateByAngle(doa);
                auto thinkingAnimationConductor = std::make_shared<DefaultAnimationConductor>(
                    std::vector<const std::shared_ptr<AnimationComposition>>{
                        std::make_shared<ListeningToThinkingAnimationComposition>(thinkingRing,
                                                                                  aliceGifLedAnimations_,
                                                                                  ledDevices_->getLedScreen(),
                                                                                  ledManager_),
                        std::make_shared<ThinkingAnimationComposition>(thinkingRing,
                                                                       aliceGifLedAnimations_,
                                                                       ledDevices_->getLedScreen(),
                                                                       ledManager_)}, SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<thinking>");
                lastSuggestedAnimation_ = ledAnimation;
                YIO_LOG_INFO("Conductor to play: " << thinkingAnimationConductor->getName());
                ledManager_->play(std::move(thinkingAnimationConductor));
                listeningPattern_->manualResetAnimation();
                break;
            }
            listeningPattern_->manualResetAnimation();
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::ALARM: {
            auto configuringAnimationConductor = std::make_shared<AlarmAnimationConductor>(
                ledDevices_->getLedScreen(),
                ledManager_,
                aliceGifLedAnimations_);
            playConductor(ledAnimation, std::move(configuringAnimationConductor));
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::TIMER: {
            auto timerBell = GifAnimation::readGif(
                aliceGifLedAnimations_ + "timer/25x16_timer_bell.gif", ledDevices_->getLedScreen());
            auto timerLoop = std::make_shared<RepeatedAnimation>(
                GifAnimation::readGif(
                    aliceGifLedAnimations_ + "timer/25x16_timer_loop.gif", ledDevices_->getLedScreen()),
                RepeatedAnimation::INFINITE);
            auto composition = std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{
                    ledManager_->getPattern("timer.led"),
                    std::make_shared<CompoundAnimation>(
                        std::vector<std::shared_ptr<Animation>>{
                            std::move(timerBell),
                            std::move(timerLoop)},
                        ledDevices_->getLedScreen())},
                false);

            auto conductor = std::make_shared<DefaultAnimationConductor>(composition);
            conductor->setSubstitutionType(SubstitutionType::BACKGROUND_DEFAULT);
            playConductor(ledAnimation, std::move(conductor));
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::TIMER_COUNTDOWN: {
            if (lastSuggestedAnimation_ != ledAnimation || isBoot_) {
                if (!state.sdkState.timers.empty()) {
                    if (auto timerToShow = showTimer(state.sdkState.timers)) {
                        auto timerConductor = std::make_shared<TimerConductor>(
                            aliceGifLedTemplates_,
                            aliceFrontalAnimationFontsPath_,
                            ledManager_,
                            ledDevices_->getLedScreen(),
                            timerToShow.value());
                        playConductor(ledAnimation, std::move(timerConductor));
                    }
                }
            }
            break;
        }

        case ExtendedDeviceState::SuggestedLedAnimation::CONFIGURING: {
            if (lastSuggestedAnimation_ != ledAnimation || isBoot_ || configuringSubstate_ != configuringSubstate) {
                configuringSubstate_ = configuringSubstate;
                switch (configuringSubstate) {
                    case ConfiguringSubstate::CONFIGURING: {
                        ledManager_->stop();

                        auto configurePattern = ledManager_->getPattern("configure.led");
                        auto configuringAnimationConductor = std::make_shared<ConfiguringAnimationConductor>(
                            aliceGifLedAnimations_,
                            ledDevices_->getLedScreen(),
                            ledManager_,
                            configurePattern);
                        playConductor(ledAnimation, std::move(configuringAnimationConductor));
                        break;
                    }
                    case ConfiguringSubstate::CONNECTING_WIFI: {
                        ledManager_->stop();
                        auto connectingWifiAnimationConductor = std::make_shared<DefaultAnimationConductor>(
                            ledManager_->getPattern("activating.led"));
                        connectingWifiAnimationConductor->setSubstitutionType(SubstitutionType::SUBSTITUTION);
                        ledManager_->play(std::move(connectingWifiAnimationConductor));
                        configuringSubstate_ = ConfiguringSubstate::CONNECTING_WIFI;
                        break;
                    }
                    case ConfiguringSubstate::SOUND_DATA_TRANSFER: {
                        auto soundinitConductor = std::make_shared<DefaultAnimationConductor>(
                            ledManager_->getPattern("soundinit.led"));
                        soundinitConductor->setSubstitutionType(SubstitutionType::SUBSTITUTION);
                        ledManager_->play(std::move(soundinitConductor));
                        configuringSubstate_ = ConfiguringSubstate::CONFIGURING;
                        break;
                    }
                    case ConfiguringSubstate::CONFUGURING_ERROR: {
                        auto pattern = ledManager_->getPattern("error.led");
                        pattern->setDuration(3000);
                        auto errorConductor = std::make_shared<DefaultAnimationConductor>(pattern);
                        errorConductor->setSubstitutionType(SubstitutionType::SUBSTITUTION);
                        ledManager_->play(std::move(errorConductor));
                        // transient states do not persist, i.e. error right after error is still shown
                        configuringSubstate_ = ConfiguringSubstate::CONFIGURING;
                        break;
                    }
                }
            }
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::UPDATE_DOWNLOADING:
            /* Simply change pattern progress. Pattern should be passed to ledManager_ just once */
            updateAnimationConductor_->setProgress(state.sdkState.updateState.downloadProgress);
        case ExtendedDeviceState::SuggestedLedAnimation::UPDATE_APPLYING: {
            playConductor(ledAnimation, updateAnimationConductor_);
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_AVAILABLE: {
            auto foregroundConductor = std::make_shared<DefaultAnimationConductor>(ledManager_->getPattern("notification_available.led"));
            foregroundConductor->setSubstitutionType(SubstitutionType::FOREGROUND);
            playConductor(ledAnimation, foregroundConductor);
            lastSuggestedAnimation_ = ledAnimation;
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_PASSIVE: {
            const bool forcePlay = clockDisplayStateChanged_;
            playConductor(ledAnimation, prepareIdleAnimation(state.sdkState, ledAnimation, true), forcePlay);
            lastSuggestedAnimation_ = ledAnimation;
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::IOT_DISCOVERY_IN_PROGRESS: {
            // This led animation is equal to THINKING animation
            playPattern(ledAnimation, ledManager_->getPattern("thinking.led"));
            break;
        }
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::MULTIROOM_CLOCK_IS_SYNCHRONIZING1: {
            playPattern(ledAnimation, ledManager_->getPattern("clock_sync_1.led"));
            break;
        }
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::MULTIROOM_CLOCK_IS_SYNCHRONIZING2: {
            playPattern(ledAnimation, ledManager_->getPattern("clock_sync_2.led"));
            break;
        }
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_RINGING_OUTGOING:
            // TODO, fallthrough now
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_RINGING_INCOMING: {
            auto appearance = GifAnimation::readGif(aliceGifLedAnimations_ + "calls/call_incoming_appearance.gif", ledDevices_->getLedScreen());
            auto ringing = std::make_shared<RepeatedAnimation>(GifAnimation::readGif(aliceGifLedAnimations_ + "calls/call_incoming_ringing.gif", ledDevices_->getLedScreen()), RepeatedAnimation::INFINITE);
            auto led = ledManager_->getPattern("call_incoming.led");

            auto compositionAppearance = std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{appearance, led},
                false);

            auto compositionRinging = std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{ringing, led},
                false);

            auto conductor = std::make_shared<DefaultAnimationConductor>(
                std::vector<const std::shared_ptr<AnimationComposition>>{compositionAppearance, compositionRinging});

            playConductor(ledAnimation, conductor);
            break;
        }
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_CONNECTING:
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_CONNECTED: {
            auto gif = std::make_shared<RepeatedAnimation>(GifAnimation::readGif(aliceGifLedAnimations_ + "calls/call_active.gif", ledDevices_->getLedScreen()), RepeatedAnimation::INFINITE);
            auto led = ledManager_->getPattern(ledAnimation == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_CONNECTED ? "call_connected.led" : "call_connecting.led");

            auto composition = std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{gif, led},
                false);

            playConductor(ledAnimation, std::make_shared<DefaultAnimationConductor>(composition));
            break;
        }
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_ENDED_OK:
        case YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_ENDED_FAILURE: {
            auto gif = GifAnimation::readGif(aliceGifLedAnimations_ + "calls/call_ended.gif", ledDevices_->getLedScreen());
            auto led = ledManager_->getPattern(ledAnimation == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::CALL_ENDED_OK ? "call_ended.led" : "error.led");

            auto composition = std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{gif, led},
                false);

            auto conductor = std::make_shared<DefaultAnimationConductor>(composition);
            conductor->setSubstitutionType(SubstitutionType::SUBSTITUTION);

            playConductor(ledAnimation, conductor);
            break;
        }
        case ExtendedDeviceState::SuggestedLedAnimation::AUDIO_PLAYING:
            if (isSpectrogramAnimationEnabled_) {
                std::vector<const std::shared_ptr<AnimationComposition>> compositions;

                if (!activeDrawLedScreenConductor_ && (lastSuggestedAnimation_ == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::LISTENING || lastSuggestedAnimation_ == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::SPEAKING)) {
                    compositions.push_back(std::make_shared<OknyxOutAnimationComposition>(aliceGifLedAnimations_,
                                                                                          ledDevices_->getLedScreen(),
                                                                                          ledManager_));
                }

                compositions.push_back(std::make_shared<DefaultAnimationComposition>(
                    std::vector<std::shared_ptr<Animation>>{spectrogramAnimation_, ledManager_->getIdlePattern()},
                    false));

                playConductor(ledAnimation, std::make_shared<DefaultAnimationConductor>(compositions, SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<audio>"), true);
                break;
            }

            playIdleAnimation(state.sdkState, ledAnimation);
            break;
        case ExtendedDeviceState::SuggestedLedAnimation::CONFIGURED:
            ledManager_->stop();
            configuringSubstate_ = ConfiguringSubstate::CONFIGURING;

            playIdleAnimation(state.sdkState, ledAnimation);
            break;
        case ExtendedDeviceState::SuggestedLedAnimation::IDLE:
        default: {
            playIdleAnimation(state.sdkState, ledAnimation);
            break;
        }
    }
    if (isBoot && !isBoot_) {
        setLedRingBrightness(ledRingBrightness_);
    }
}

void DeviceStateHandler::playPattern(const ExtendedDeviceState::SuggestedLedAnimation& ledAnimation,
                                     std::shared_ptr<Animation> pattern,
                                     bool forcePlay) {
    /* Update animation if state changed */
    if (lastSuggestedAnimation_ != ledAnimation || isBoot_ || forcePlay) {
        isBoot_ = false;
        lastSuggestedAnimation_ = ledAnimation;
        YIO_LOG_INFO("Pattern to play: " << pattern->getName());
        /* All SuggestedAnimations currently are background */

        auto emptyAnimation = std::make_shared<EmptyAnimation>(ledDevices_->getLedScreen());

        auto composition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{emptyAnimation, pattern},
            false);

        auto conductor = std::make_shared<DefaultAnimationConductor>(composition);

        ledManager_->play(std::move(conductor));
    }
}

void DeviceStateHandler::playConductor(const ExtendedDeviceState::SuggestedLedAnimation& ledAnimation,
                                       std::shared_ptr<AnimationConductor> conductor,
                                       bool forcePlay) {
    /* Update animation if state changed */
    if (lastSuggestedAnimation_ != ledAnimation || isBoot_ || forcePlay) {
        isBoot_ = false;
        lastSuggestedAnimation_ = ledAnimation;
        YIO_LOG_INFO("Conductor to play: " << conductor->getName());
        ledManager_->play(std::move(conductor));
    }
}

void DeviceStateHandler::onSoundDataTransferStart() {
    cbQueue_.add([this]() { animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, ConfiguringSubstate::SOUND_DATA_TRANSFER); });
}

void DeviceStateHandler::onConnectingToNetwork() {
    cbQueue_.add([this]() { animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, ConfiguringSubstate::CONNECTING_WIFI); });
}

void DeviceStateHandler::onSetupError() {
    cbQueue_.add([this]() { animateDeviceState(deviceState_, deviceState_.sdkState.configurationState, ConfiguringSubstate::CONFUGURING_ERROR); });
}

void DeviceStateHandler::onSpectrum(int64_t playerInstanceId, const SpectrumFrame& frame, SpectrumType /* type */) {
    cbQueue_.add([this, playerInstanceId, frame]() {
        if (isSpectrogramAnimationEnabled_) {
            if (spectrogramPlayerInstanceId_ == playerInstanceId) {
                spectrogramAnimation_->onSpectrum(frame);
            }
        }
    });
}

void DeviceStateHandler::onSpectrumStart(int64_t playerInstanceId, SpectrumType /* type */) {
    YIO_LOG_INFO("Spectrum started, resetting animation");
    cbQueue_.add([this, playerInstanceId]() {
        spectrogramPlayerInstanceId_ = playerInstanceId;
        spectrogramAnimation_->resetAnimation();
        spectrogramAnimation_->setPaused(false);
    });
}

void DeviceStateHandler::onSpectrumPause(int64_t playerInstanceId, SpectrumType /* type */) {
    YIO_LOG_INFO("Spectrum paused");
    cbQueue_.add([this, playerInstanceId]() {
        if (spectrogramPlayerInstanceId_ == playerInstanceId) {
            spectrogramAnimation_->setPaused(true);
        }
    });
}

void DeviceStateHandler::onSpectrumResume(int64_t playerInstanceId, SpectrumType /* type */) {
    YIO_LOG_INFO("Spectrum resumed");
    cbQueue_.add([this, playerInstanceId]() {
        spectrogramPlayerInstanceId_ = playerInstanceId;
        spectrogramAnimation_->setPaused(false);
    });
}

void DeviceStateHandler::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    cbQueue_.add([this, configName, jsonConfigValue]() {
        processSystemConfig(configName, jsonConfigValue);
    });
}

void DeviceStateHandler::onDeviceConfig(const std::string& configName, const std::string& jsonConfigValue) {
    cbQueue_.add([this, configName, jsonConfigValue]() {
        processDeviceConfig(configName, jsonConfigValue);
    });
}

void DeviceStateHandler::processDeviceConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName == ConfigUtils::LED_USER_CONFIG) {
        YIO_LOG_INFO("processDeviceConfig received " << configName << ": " << jsonConfigValue);
        try {
            const auto config = parseJson(jsonConfigValue);
            if (config.isMember("music_equalizer_visualization")) {
                const auto& userConfig = config["music_equalizer_visualization"];
                auto current = (userConfig["auto"].asBool() ? "random" : userConfig["style"].asString());
                spectrogramAnimation_->switchLocalCurrentPreset(std::move(current));
            }
            if (config.isMember("time_visualization")) {
                clockAnimation_->setFontConfig(ConfigUtils::getClockFont(config["time_visualization"], aliceFrontalAnimationFontsPath_));
            }
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("DeviceStateHandler.processDeviceConfig.LedConfig.InvalidJson", "Can't parse config " << configName << e.what());
        }
    }
}

void DeviceStateHandler::processSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName == ConfigUtils::SPECTROGRAM_ANIMATION_CONFIG) {
        YIO_LOG_INFO("processSystemConfig received " << configName << ": " << jsonConfigValue);
        try {
            const auto config = parseJson(jsonConfigValue);
            isSpectrogramAnimationEnabled_ = config["enabled"].asBool();
            Json::Value presets;
            if (config.isMember("presets")) {
                presets = config["presets"];
            }
            spectrogramAnimation_->updateLocalPresets(quasar::jsonToString(presets));
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("DeviceStateHandler.SpectrogramConfig.InvalidJson", "Can't parse config " << configName << e.what());
        }

        YIO_LOG_INFO("Spectrogram animation: " << (isSpectrogramAnimationEnabled_ ? "on" : "off"));
    } else if (configName == ConfigUtils::LED_USER_CONFIG) {
        YIO_LOG_INFO("processSystemConfig received " << configName << ": " << jsonConfigValue);
        try {
            const auto config = parseJson(jsonConfigValue);
            const auto bootLedRingBrightness = tryGetInt(config, "bootLedRingBrightness", DEFAULT_BOOT_LED_RING_BRIGHTNESS);
            setBootLedRingBrightness(bootLedRingBrightness);
            ledRingBrightness_ = tryGetInt(config, "ledRingBrightness", DEFAULT_LED_RING_BRIGHTNESS);
            setLedRingBrightness(ledRingBrightness_);
            dimLedRingBrightness_ = tryGetInt(config, "dimLedRingBrightness", DEFAULT_DIM_LED_RING_BRIGHTNESS);
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("DeviceStateHandler.processSystemConfig.LedConfig.InvalidJson", "Can't parse config " << configName << e.what());
        }
    } else if (configName == ConfigUtils::DEVICE_STATE_HANDLER_CONFIG) {
        YIO_LOG_INFO("processSystemConfig received " << configName << ": " << jsonConfigValue);
        try {
            const auto config = parseJson(jsonConfigValue);
            if (const auto val = tryGetInt(config, "showClockAfterMs", emptyAnimationLength_.count()); val != emptyAnimationLength_.count()) {
                YIO_LOG_INFO("New empty animation length: " << val << " ms");
                emptyAnimationLength_ = std::chrono::milliseconds(val);
            }
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("DeviceStateHandler.processSystemConfig.DeviceStateHandlerConfig.InvalidJson",
                                "Can't parse config " << configName << e.what());
        }
    }
}

void DeviceStateHandler::setDoaAngle(double doa)
{
    doa_.store(doa);
    listeningPattern_->setDoaAngle(doa);
}

std::shared_ptr<VolumeConductor> DeviceStateHandler::getVolumeConductor() {
    return volumeConductor_;
}

std::shared_ptr<RcuVolumeConductor> DeviceStateHandler::getRcuVolumeConductor() {
    return rcuVolumeConductor_;
}

std::shared_ptr<AnimationConductor> DeviceStateHandler::prepareIdleAnimation(const YandexIO::SDKState& state, const ExtendedDeviceState::SuggestedLedAnimation& ledAnimation, bool skipEmpty) {
    std::vector<const std::shared_ptr<AnimationComposition>> compositions;

    std::shared_ptr<Animation> pattern;
    bool showLedPattern = false;
    if (ledAnimation == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::NOTIFICATION_PASSIVE) {
        pattern = ledManager_->getPattern("notification_passive.led");
        showLedPattern = true;
    } else if (ledAnimation == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::BLUETOOTH_IS_PLAYING) {
        pattern = ledManager_->getPattern("bluetooth_playing.led");
        showLedPattern = true;
    } else {
        pattern = ledManager_->getIdlePattern();
    }

    if (!activeDrawLedScreenConductor_ && (lastSuggestedAnimation_ == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::LISTENING || lastSuggestedAnimation_ == YandexIO::ExtendedDeviceState::SuggestedLedAnimation::SPEAKING)) {
        compositions.push_back(std::make_shared<OknyxOutAnimationComposition>(aliceGifLedAnimations_,
                                                                              ledDevices_->getLedScreen(),
                                                                              ledManager_));
    }

    const bool clockSynchronized = state.ntpdState.clockSynchronized.has_value() && state.ntpdState.clockSynchronized.value();
    if (clockDisplayState_.isClockOn && (clockSynchronizedOnce_ || clockSynchronized)) {
        if (!skipEmpty && !SKIP_EMPTY_STATES.count(lastSuggestedAnimation_) && !clockDisplayStateChanged_) {
            auto emptyAnimation = std::make_shared<EmptyAnimation>(ledDevices_->getLedScreen());
            const auto tp = std::chrono::steady_clock::now() + emptyAnimationLength_;
            emptyAnimation->setFinishedTimepoint(tp);
            compositions.push_back(std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{emptyAnimation, pattern},
                false));
        }

        if (showLedPattern) {
            compositions.push_back(std::make_shared<DefaultAnimationComposition>(
                std::vector<std::shared_ptr<Animation>>{clockAnimation_, pattern},
                false));
        } else {
            compositions.push_back(std::make_shared<IdleAnimationComposition>(clockAnimation_, *ledManager_));
        }

    } else {
        if (!clockDisplayState_.isClockOn) {
            YIO_LOG_INFO("Clock is off, not showing clock");
        } else {
            YIO_LOG_WARN("Clock not synchronized, not showing clock");
        }
        auto emptyAnimation = std::make_shared<EmptyAnimation>(ledDevices_->getLedScreen());
        compositions.push_back(std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{emptyAnimation, pattern},
            false));
    }

    clockDisplayStateChanged_ = false;

    return std::make_shared<DefaultAnimationConductor>(compositions, SubstitutionType::BACKGROUND_DEFAULT, FinishingType::FIRST, "<idle>");
}

std::optional<std::chrono::system_clock::time_point> DeviceStateHandler::showTimer(const std::vector<SDKState::TimerState>& timers) {
    auto now = std::chrono::system_clock::now();

    std::optional<std::chrono::system_clock::time_point> result;
    static constexpr std::chrono::milliseconds timerSwitchSlack = std::chrono::milliseconds(600);
    for (const auto& timer : timers) {
        const auto endTs = timer.endTs;
        if (endTs > (now - timerSwitchSlack) && (result == std::nullopt || result > endTs) && endTs <= (now + (std::chrono::hours(1) + std::chrono::seconds(1))))
        {
            result = endTs;
        }
    }

    return result;
}

void DeviceStateHandler::playIdleAnimation(const SDKState& state, const ExtendedDeviceState::SuggestedLedAnimation& ledAnimation) {
    const bool clockSynchronized = state.ntpdState.clockSynchronized.has_value() && state.ntpdState.clockSynchronized.value();
    const bool forcePlay = (clockSynchronized && !clockSynchronizedOnce_) || clockDisplayStateChanged_;
    playConductor(ledAnimation, prepareIdleAnimation(state, ledAnimation), forcePlay);

    if (clockSynchronized) {
        clockSynchronizedOnce_ = true;
    }
}

DeviceStateHandler::~DeviceStateHandler() noexcept {
    cbQueue_.destroy(); // finish all callbacks
}
