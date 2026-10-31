#pragma once

#include "rcu_buttons_controller.h"
#include "rcu_button.h"

#include <smart_devices/platforms/yandexstation_2/platformd/audio_input/audio_input_controller.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/draw_led_screen_directive_observer.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/yandex_station2_bluetooth_observer.h>
#include <smart_devices/platforms/yandexstation_2/platformd/foreground/foreground_activity.h>

#include <yandex_io/modules/buttons/buttons.h>
#include <yandex_io/modules/buttons/click_handlers/button_multiple_click_handler.h>
#include <yandex_io/modules/audio_input/mics_state/hw_provider/hw_mics_state_provider.h>
#include <yandex_io/modules/demo_mode/demo_mode_handler.h>
#include <yandex_io/modules/quasar_device_state/quasar_device_state_holder.h>
#include <yandex_io/modules/volume_manager/base/volume_manager.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/libs/wifi_reseter/wifi_reseter.h>
#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <map>
#include <memory>
#include <string>

class ButtonsHandler final {
public:
    ButtonsHandler(
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
        std::shared_ptr<DemoModeHandler> demoModeHandler);

    void setSingleClickActivationEnabled(bool singleClickActivationEnabled);
    YandexIO::MicsStateProvider& micsStateProvider();
    /**
     * @brief Provides current mics state and start polling buttons
     */
    void start();

    ~ButtonsHandler();

    struct EventSource {
        static constexpr const char* BUTTON = "button";
        static constexpr const char* RCU_PRESS = "rcu_press";
        static constexpr const char* RCU_HOLD = "rcu_hold";
    };

private:
    enum class ButtonsIdx {
        VOLUME_UP,
        VOLUME_DOWN,
        ACTIVATE,
        MUTE
    };

    void handleButtonMessage(const quasar::ipc::SharedMessage& message);
    void handleButtonEvent(int idx, Buttons::ButtonEvent evt, std::chrono::milliseconds timeDiff);

    static const std::map<ButtonsIdx, std::string> BUTTONS_NAMES;
    static constexpr auto LONG_PRESS_TIMEOUT = std::chrono::seconds(5);

    const std::shared_ptr<YandexIO::SDKInterface> sdk_;
    const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    YandexIO::HwMicsStateProvider micsStateProvider_;
    std::unique_ptr<Buttons> buttonsImpl_;
    std::shared_ptr<VolumeManager> volumeManager_;
    std::shared_ptr<YandexStation2BluetoothObserver> bluetoothController_;
    std::shared_ptr<QuasarDeviceStateHolder> quasarStateHolder_;
    std::shared_ptr<ForegroundActivity> foregroundActivity_;
    std::shared_ptr<RcuButtonsController> rcuButtonsController_;
    std::weak_ptr<AudioInputController> audioInputController_;
    ButtonMultipleClickHandler multipleClickHandler_;
    class RcuDialogObserversHolder;
    std::shared_ptr<RcuDialogObserversHolder> dialogObservers_;

    int32_t lastRcuButton_{-1};
    quasar::proto::ButtonEvent_Action lastRcuButtonAction_{quasar::proto::ButtonEvent_Action_UP};
    bool lastRcuButtonLongPress_{false};
    std::shared_ptr<quasar::ipc::IConnector> rcuButtonsdConnector_;
    std::atomic_bool singleClickActivationEnabled_{false};

    std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenDirectiveObserver_;
    std::shared_ptr<DemoModeHandler> demoModeHandler_;
    std::shared_ptr<quasar::WifiReseter> wifiReseter_;
};
