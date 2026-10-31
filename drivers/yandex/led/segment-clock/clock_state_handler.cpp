#include "clock_state_handler.h"

#include <memory>
#include <yandex_io/libs/json_utils/json_utils.h>

namespace {
    constexpr auto DND_MODE_CONFIG_NAME = "dndMode";
}

std::shared_ptr<ClockStateHandler> ClockStateHandler::create(std::shared_ptr<YandexIO::SDKInterface> sdk, const std::string& stateFile, const ClockStateHandler::BrightnessSettings& settings) {
    std::shared_ptr<ClockStateHandler> clockStateHandler(new ClockStateHandler(sdk, stateFile, settings));
    clockStateHandler->subscribeToSdk(*sdk);
    return clockStateHandler;
}

ClockStateHandler::ClockStateHandler(std::shared_ptr<YandexIO::SDKInterface> sdk, const std::string& stateFile, const BrightnessSettings& settings)
    : brightnessConfig_(settings.brightnessCurve, settings.minBrightness, settings.maxBrightness)
    , brightnessAnimator_(std::bind(&ClockStateHandler::setBrightness, this, std::placeholders::_1))
    , state_(TFsPath(stateFile), std::move(sdk))
    , maxBrightness_(settings.maxBrightness)
    , configName_(settings.configName)
{
}

void ClockStateHandler::subscribeToSdk(YandexIO::SDKInterface& sdk) {
    sdk.addDirectiveObserver(weak_from_this());
    sdk.addBackendConfigObserver(weak_from_this());
    sdk.addSDKStateObserver(weak_from_this());
    sdk.subscribeToDeviceConfig(configName_);
    sdk.subscribeToDeviceConfig(DND_MODE_CONFIG_NAME);
}

void ClockStateHandler::onBrightnessChanged(float sensorBrightness) {
    auto level = brightnessConfig_.getLevel(sensorBrightness);
    brightnessAnimator_.animate(level);
}

void ClockStateHandler::setBrightness(uint32_t brightness) {
    state_.setBrightness(static_cast<double>(brightness) / maxBrightness_);
}

void ClockStateHandler::addListener(std::weak_ptr<ClockDisplayStateHolder::Listener> listener) {
    state_.addListener(std::move(listener));
}

void ClockStateHandler::onDeviceConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName == configName_) {
        if (const auto config = quasar::tryParseJson(jsonConfigValue); config.has_value()) {
            brightnessConfig_.updateUserConfig(config.value()["brightness"]);
        }
    } else if (configName == DND_MODE_CONFIG_NAME) {
        if (const auto config = quasar::tryParseJson(jsonConfigValue); config.has_value()) {
            const auto showClock = quasar::tryGetBool(config.value()["platformSettings"], "showClock", true);
            const auto clockState = state_.getClockDisplayState();

            if (showClock != clockState.showClockDuringDnd) {
                updateClockState(clockState.clockEnabled, showClock);
            }
        }
    }
}

void ClockStateHandler::onDirective(const std::string& name, const std::string& /* vinsRequestId */, const std::string& /* jsonPayload */) {
    if (name == quasar::Directives::SHOW_CLOCK || name == quasar::Directives::HIDE_CLOCK) {
        const auto state = state_.getClockDisplayState();
        const bool enabled = name == quasar::Directives::SHOW_CLOCK;
        // directive has priority over dnd. So turn on clock after directive even if currently in dnd mode
        state_.setClockBehaviour(enabled, enabled, state.showClockDuringDnd);
    }
}

void ClockStateHandler::onSDKState(const YandexIO::SDKState& state) {
    if (isDndMode_ == state.isDoNotDisturbMode) {
        return;
    }
    isDndMode_ = state.isDoNotDisturbMode;
    const auto clockState = state_.getClockDisplayState();
    updateClockState(clockState.clockEnabled, clockState.showClockDuringDnd);
}

void ClockStateHandler::updateClockState(bool enabled, bool showClockDuringDnd) {
    if (!enabled || (isDndMode_ && !showClockDuringDnd)) {
        state_.setClockBehaviour(false, enabled, showClockDuringDnd);
    } else {
        state_.setClockBehaviour(true, enabled, showClockDuringDnd);
    }
}
