#pragma once

#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/directive_observer.h>
#include <yandex_io/sdk/clock_display_state.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <yandex_io/libs/base/directives.h>

#include <yandex_io/modules/clock_display_state_holder/clock_display_state_holder.h>

#include <smart_devices/libs/brightness_control/brightness_control.h>
#include <smart_devices/libs/brightness_control/brightness_animator.h>
#include <smart_devices/libs/brightness_control/brightness_config.h>

#include <memory>
#include <vector>

class ClockStateHandler: public YandexIO::SDKStateObserver,
                         public YandexIO::BackendConfigObserver,
                         public YandexIO::DirectiveObserver,
                         public BrightnessControl::Listener,
                         public std::enable_shared_from_this<ClockStateHandler> {
public:
    struct BrightnessSettings {
        uint32_t minBrightness = 0;
        uint32_t maxBrightness = 170;
        BrightnessConfig::LinearCurve brightnessCurve = {
            {0.0f, 3},
            {1.0f, 30},
            {10.0f, 100},
            {50.0f, 150},
            {120.0f, 170},
            {256.0f, 170}};
        std::string configName = "led";
    };

    [[nodiscard]] static std::shared_ptr<ClockStateHandler> create(std::shared_ptr<YandexIO::SDKInterface> sdk, const std::string& stateFile, const BrightnessSettings& settings);

    void addListener(std::weak_ptr<ClockDisplayStateHolder::Listener> listener);

private:
    ClockStateHandler(std::shared_ptr<YandexIO::SDKInterface> sdk, const std::string& stateFile, const ClockStateHandler::BrightnessSettings& settings);
    void subscribeToSdk(YandexIO::SDKInterface& sdk);

private:
    void onBrightnessChanged(float sensorBrightness) override;
    void setBrightness(uint32_t brightness);
    void onDeviceConfig(const std::string& configName, const std::string& jsonConfigValue) override;
    void onDirective(const std::string& name, const std::string& vinsRequestId, const std::string& jsonPayload) override;

    void onSDKState(const YandexIO::SDKState& state) override;
    void updateClockState(bool enabled, bool showClockDuringDnd);

private:
    bool isDndMode_{false};

    BrightnessConfig brightnessConfig_;
    BrightnessAnimator brightnessAnimator_;
    ClockDisplayStateHolder state_;
    const uint32_t maxBrightness_;
    const std::string configName_;
};
