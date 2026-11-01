#include "buttons_handler.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/yandex_station2_bluetooth_observer.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/modules/buttons/dev_input/dev_input_buttons.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/model_objects.pb.h>

#include <functional>
#include <string>

using namespace quasar;
using namespace YandexIO;

namespace {
    constexpr auto BLUETOOTH_ENABLE_THRESHOLD = std::chrono::milliseconds(300);
} // namespace

const std::map<ButtonsHandler::ButtonsIdx, std::string> ButtonsHandler::BUTTONS_NAMES = {
    {ButtonsIdx::ACTIVATE, "activate"},
    {ButtonsIdx::MUTE, "mute"},
    {ButtonsIdx::VOLUME_UP, "volume_up"},
    {ButtonsIdx::VOLUME_DOWN, "volume_down"}};

class ButtonsHandler::RcuDialogObserversHolder: public YandexIO::SDKStateObserver {
    ButtonsHandler* buttonsHandler_;
    std::mutex mutex_;
    std::optional<int> tag_;
    YandexIO::SDKState::AliceState::State state_{YandexIO::SDKState::AliceState::State::NONE};

    void handleListening() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tag_) {
            if (auto scontroller = buttonsHandler_->audioInputController_.lock()) {
                scontroller->startRcuCapture(tag_.value(), [this]() {
                    /* Finish request voice input when received last chunk */
                    buttonsHandler_->sdk_->getAliceCapability()->finishConversationVoiceInput();
                });
            }
            tag_.reset();
        };
    }

public:
    RcuDialogObserversHolder(ButtonsHandler* handler)
        : buttonsHandler_(handler)
    {
    }

    void onSDKState(const YandexIO::SDKState& state) override {
        if (state.aliceState.state == state_) {
            return;
        }
        state_ = state.aliceState.state;
        if (state_ == YandexIO::SDKState::AliceState::State::LISTENING) {
            handleListening();
        }
    }

    void onConversationError() override {
        // no-op
    }

    void newTag(int tag) {
        std::lock_guard<std::mutex> lock(mutex_);
        tag_ = tag;
    }
};

ButtonsHandler::ButtonsHandler(
    std::shared_ptr<YandexIO::SDKInterface> sdk,
    std::shared_ptr<YandexIO::ITelemetry> telemetry,
    const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
    std::shared_ptr<VolumeManager> volumeManager,
    std::shared_ptr<YandexStation2BluetoothObserver> bluetoothController,
    std::shared_ptr<QuasarDeviceStateHolder> quasarStateHolder,
    std::shared_ptr<ForegroundActivity> foregroundActivity,
    std::shared_ptr<RcuButtonsController> rcuButtonsController,
    std::weak_ptr<AudioInputController> audioInputController,
    std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenDirectiveObserver,
    std::shared_ptr<DemoModeHandler> demoModeHandler)
    : sdk_(std::move(sdk))
    , telemetry_(telemetry)
    , buttonsImpl_(std::make_unique<YandexIO::DevInputButtons>(
          std::vector<std::string>{"/dev/input/event2", "/dev/input/event0"},
          std::map<uint16_t, int>{
              std::make_pair(109, int(ButtonsIdx::VOLUME_DOWN)),
              std::make_pair(104, int(ButtonsIdx::VOLUME_UP)),
              std::make_pair(582, int(ButtonsIdx::ACTIVATE)),
              std::make_pair(248, int(ButtonsIdx::MUTE))},
          std::vector<YandexIO::DevInputButtons::ButtonInfo>({YandexIO::DevInputButtons::ButtonInfo("/dev/input/event0", 248)})))
    , volumeManager_(std::move(volumeManager))
    , bluetoothController_(std::move(bluetoothController))
    , quasarStateHolder_(std::move(quasarStateHolder))
    , foregroundActivity_(std::move(foregroundActivity))
    , rcuButtonsController_(std::move(rcuButtonsController))
    , audioInputController_(std::move(audioInputController))
    , dialogObservers_(std::make_shared<RcuDialogObserversHolder>(this))
    , rcuButtonsdConnector_(ipcFactory->createIpcConnector("rcu_buttonsd"))
    , drawLedScreenDirectiveObserver_(std::move(drawLedScreenDirectiveObserver))
    , demoModeHandler_(std::move(demoModeHandler))
    , wifiReseter_(makeWifiReseter(sdk_, std::move(telemetry), makeStationWifiSwitcher(ipcFactory)))
{
    Y_VERIFY(volumeManager_ != nullptr);

    sdk_->addSDKStateObserver(dialogObservers_);

    buttonsImpl_->setSendButtonEventCallback(std::bind(&ButtonsHandler::handleButtonEvent, this,
                                                       std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    auto onSingleClick = [this](std::chrono::milliseconds timeDiff) {
        if (timeDiff < LONG_PRESS_TIMEOUT) {
            /* Start Conversation with Alice. No need to start it if timeDiff > LONG_PRESS_TIMEOUT,
             * because device will be in SetUp mode
             */
            sdk_->getAliceCapability()->toggleConversation(VinsRequest::createHardwareButtonClickEventSource());
        }
    };

    multipleClickHandler_.addMultipleClickHandler(static_cast<int>(ButtonsIdx::ACTIVATE), 5, onSingleClick, [this]() {
        if (!bluetoothController_->isDiscoverable()) {
            bluetoothController_->setVisibility(true, true);
        } else {
            bluetoothController_->setVisibility(true, false);
        }
    }, BLUETOOTH_ENABLE_THRESHOLD);

    // FIXME: remove or replace this ugly *sextaclick* [SK-5788]
    multipleClickHandler_.addMultipleClickHandler(int(ButtonsIdx::ACTIVATE), 6, onSingleClick, [this]() {
        wifiReseter_->resetWifiByButton();
    }, BLUETOOTH_ENABLE_THRESHOLD);

    rcuButtonsdConnector_->setMessageHandler(std::bind(&ButtonsHandler::handleButtonMessage, this, std::placeholders::_1));
}

ButtonsHandler::~ButtonsHandler()
{
    buttonsImpl_->stop();
    rcuButtonsdConnector_->shutdown();
}

void ButtonsHandler::start()
{
    /* Start polling buttons. DevInputButtons will get Mute state before start */
    buttonsImpl_->start();
    rcuButtonsdConnector_->connectToService();
}

YandexIO::MicsStateProvider& ButtonsHandler::micsStateProvider() {
    return micsStateProvider_;
}

void ButtonsHandler::handleButtonEvent(int idx, Buttons::ButtonEvent evt, std::chrono::milliseconds timeDiff)
{
    const auto buttonIdx = static_cast<ButtonsIdx>(idx);
    if (buttonIdx != ButtonsIdx::VOLUME_UP && buttonIdx != ButtonsIdx::VOLUME_DOWN) {
        YIO_LOG_INFO("Button Event. Button: " << BUTTONS_NAMES.at(buttonIdx) << " Event: " << (int)evt << " Time diff ms: " << timeDiff.count());
    }
    switch (buttonIdx) {
        case ButtonsIdx::VOLUME_UP: {
            if (evt == Buttons::ButtonEvent::PRESS_RELEASE) {
                volumeManager_->manualVolumeUp(EventSource::BUTTON);
            }
            break;
        }
        case ButtonsIdx::VOLUME_DOWN: {
            if (evt == Buttons::ButtonEvent::PRESS_RELEASE) {
                volumeManager_->manualVolumeDown(EventSource::BUTTON);
            }
            break;
        }
        case ButtonsIdx::ACTIVATE: {
            if (evt == Buttons::ButtonEvent::PRESS_RELEASE) {
                // distinguish manual button press and stop directive animation
                // otherwise normally IDLE state continues playing directive animation
                drawLedScreenDirectiveObserver_->stop();
                if (demoModeHandler_->isDemoMode()) {
                    if (quasarStateHolder_->isDeviceConfigured()) {
                        demoModeHandler_->togglePlay();
                    }
                } else {
                    multipleClickHandler_.buttonReleased(idx, timeDiff);
                }
            } else if (evt == Buttons::ButtonEvent::LONG_PRESS) {
                /* User pressed Activate button for LONG_PRESS_TIMEOUT. Start/Stop SetUp mode */
                if (!quasarStateHolder_->hasDeviceStartedDownloading()) {
                    sdk_->toggleSetupMode();
                }
            }
            break;
        }
        case ButtonsIdx::MUTE: {
            if (evt == Buttons::ButtonEvent::PRESS_RELEASE) {
                micsStateProvider_.unmute();
            } else if (evt == Buttons::ButtonEvent::PRESS_START) {
                micsStateProvider_.mute();
                /* Explicitly stop conversation when mute */
                sdk_->getAliceCapability()->finishConversationVoiceInput();
            }
            break;
        }
    }
}

void ButtonsHandler::handleButtonMessage(const quasar::ipc::SharedMessage& message)
{
    if (foregroundActivity_->handleQuasarMessage(message)) {
        return;
    }
    if (!message->has_rcu_event()) {
        return;
    }
    const auto& rcuEvent = message->rcu_event();
    if (!rcuEvent.has_button_event() ||
        !rcuEvent.button_event().has_key_code() ||
        !rcuEvent.button_event().has_action()) {
        return;
    }
    const auto keyCode = rcuEvent.button_event().key_code();
    const auto action = rcuEvent.button_event().action();
    const auto isLongPress = rcuEvent.button_event().is_long_press();
    /* do not spam with "auto repeat" */
    const bool sameButtonAction = (lastRcuButton_ == keyCode && lastRcuButtonAction_ == action);
    const bool oldButtonLongPress = lastRcuButtonLongPress_;
    lastRcuButton_ = keyCode;
    lastRcuButtonAction_ = action;
    lastRcuButtonLongPress_ = isLongPress;
    if (!sameButtonAction) {
        YIO_LOG_INFO("Got button event from an RCU. Key: " << keyCode << ", action: " << action)
    }
    const std::optional<rcu_button::Key> rcuButtonOptional = rcu_button::findKey(keyCode);
    if (!rcuButtonOptional) {
        return;
    }
    const rcu_button::Key rcuButton = rcuButtonOptional.value();
    switch (rcuButton) {
        case rcu_button::Key::VOLUME_UP:
        case rcu_button::Key::VOLUME_DOWN: {
            if (action != proto::ButtonEvent::DOWN) {
                break;
            }
            const bool isUp = (rcuButton == rcu_button::Key::VOLUME_UP);
            const auto source = isLongPress ? EventSource::RCU_HOLD : EventSource::RCU_PRESS;
            if (isUp) {
                volumeManager_->manualVolumeUp(source);
            } else {
                volumeManager_->manualVolumeDown(source);
            }
            break;
        }
        case rcu_button::Key::VOLUME_MUTE: {
            if (action == proto::ButtonEvent::UP) {
                if (volumeManager_->manualGetIsMuted()) {
                    volumeManager_->manualUnmute(EventSource::RCU_PRESS);
                } else {
                    volumeManager_->manualMute(EventSource::RCU_PRESS);
                }
            }
            break;
        }
        case rcu_button::Key::ACTIVATE: {
            const auto tag = rcuEvent.tag();
            if (action == proto::ButtonEvent::DOWN) {
                if (!isLongPress && singleClickActivationEnabled_) {
                    YIO_LOG_DEBUG("Single click activation enabled. Waiting for long press or release.")
                    break;
                }

                // NOTE this event will be fired multiple times during pressing the button
                // Don't break if singleClickActivationEnabled_ is true and we've got second event.
                // First (short) event has been skipped, so we need to react on second (long) event
                if (sameButtonAction && !(singleClickActivationEnabled_ && oldButtonLongPress != isLongPress)) {
                    break;
                }

                // TODO: Provide asr model
                dialogObservers_->newTag(tag);
                sdk_->getAliceCapability()->startConversation(VinsRequest::createHardwareButtonClickEventSource());
            } else if (action == proto::ButtonEvent::UP) {
                if (!isLongPress && singleClickActivationEnabled_) {
                    YIO_LOG_INFO("Single click activation enabled. Toggle conversation.")
                    telemetry_->reportEvent("RcuToggleConversation");
                    sdk_->getAliceCapability()->toggleConversation(VinsRequest::createHardwareButtonClickEventSource());
                }
            }
            break;
        }
        case rcu_button::Key::MAIN: {
            if (action == proto::ButtonEvent::UP) {
                rcuButtonsController_->handleMainPress();
            }
            break;
        }
        case rcu_button::Key::NAV_LEFT: {
            if (action == proto::ButtonEvent::DOWN && !sameButtonAction) {
                rcuButtonsController_->handleNavLeftPress();
            }
            break;
        }
        case rcu_button::Key::NAV_RIGHT: {
            if (action == proto::ButtonEvent::DOWN && !sameButtonAction) {
                rcuButtonsController_->handleNavRightPress();
            }
            break;
        }
        case rcu_button::Key::BACK: {
            if (action == proto::ButtonEvent::UP) {
                rcuButtonsController_->handleBackPress();
            }
            break;
        }
        default: {
            break;
        }
    }
}

void ButtonsHandler::setSingleClickActivationEnabled(bool singleClickActivationEnabled) {
    singleClickActivationEnabled_.store(singleClickActivationEnabled);
}
