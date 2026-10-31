#include "led_devices_impl.h"

#include "frontal_animation/yandex_station_frontal_controller.h"
#include "ring/yandex_station_head_controller.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/logging/logging.h>
YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

LedDevicesImpl::LedDevicesImpl(std::string deviceId,
                               std::string defaultAnimationsPath,
                               std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder)
    : deviceId_(std::move(deviceId))
    , clockDisplayStateHolder_(clockDisplayStateHolder)
    , defaultDevice_(std::make_shared<YandexStationHeadController>("YNDX LED ring"))
    , ledScreen_(std::make_shared<YandexStationFrontalController>("YNDX LED screen", std::move(clockDisplayStateHolder)))
    , devicesList_{defaultDevice_, ledScreen_}
    , defaultAnimationsPath_(std::move(defaultAnimationsPath))
{
}

const std::vector<std::shared_ptr<quasar::LedDevice>>& LedDevicesImpl::getDevicesList() const {
    return devicesList_;
}

std::shared_ptr<LedController> LedDevicesImpl::getDefaultDevice() const {
    return defaultDevice_;
}

std::shared_ptr<YandexStationFrontalController> LedDevicesImpl::getLedScreen() {
    return ledScreen_;
}

const std::string& LedDevicesImpl::getDefaultAnimationsPath() const {
    return defaultAnimationsPath_;
}

void LedDevicesImpl::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName == ConfigUtils::LED_DEVICES_CONFIG) {
        Json::Value frontalController;
        try {
            frontalController = parseJson(jsonConfigValue)["frontalController"];
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("LedDevicesImpl.FrontalControllerConfig.InvalidJson", "Can't parse " << configName << " frontalController : " << e.what());
        }
        const auto& colorConfig = frontalController["colorConfig"];
        ledScreen_->getColorConfig().updateConfig(colorConfig, ColorConfig::getDeviceColor(deviceId_));
        const auto& brightnessConfig = frontalController["brightnessConfig"];
        ledScreen_->getBrightnessConfig().updateSystemConfig(brightnessConfig);
    }
}

void LedDevicesImpl::onDeviceConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName == ConfigUtils::LED_USER_CONFIG) {
        try {
            ledScreen_->getBrightnessConfig().updateUserConfig(parseJson(jsonConfigValue)["brightness"]);
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("LedDevicesImpl.BrightnessConfig.InvalidJson", "Can't parse config " << configName << e.what());
        }
    } else if (configName == ConfigUtils::DO_NOT_DISTURB_CONFIG) {
        if (const auto config = quasar::tryParseJson(jsonConfigValue); config.has_value()) {
            const auto showClock = quasar::tryGetBool(config.value()["platformSettings"], "showClock", true);
            const auto clockState = clockDisplayStateHolder_->getClockDisplayState();

            if (showClock != clockState.showClockDuringDnd) {
                updateClockState(clockState.clockEnabled, showClock);
            }
        }
    }
}

void LedDevicesImpl::onSDKState(const YandexIO::SDKState& state) {
    if (isDndMode_ == state.isDoNotDisturbMode) {
        return;
    }
    isDndMode_ = state.isDoNotDisturbMode;
    const auto clockState = clockDisplayStateHolder_->getClockDisplayState();
    updateClockState(clockState.clockEnabled, clockState.showClockDuringDnd);
}

void LedDevicesImpl::updateClockState(bool enabled, bool showClockDuringDnd) {
    YIO_LOG_DEBUG("updateClockState. clockEnabled: " << enabled << ", showClockDuringDnd: " << showClockDuringDnd << ", isDndMode: " << isDndMode_);
    if (!enabled || (isDndMode_ && !showClockDuringDnd)) {
        clockDisplayStateHolder_->setClockBehaviour(false, enabled, showClockDuringDnd);
    } else {
        clockDisplayStateHolder_->setClockBehaviour(true, enabled, showClockDuringDnd);
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void LedDevicesImpl::subscribeToConfig(YandexIO::SDKInterface& sdk) {
    sdk.subscribeToSystemConfig(ConfigUtils::LED_DEVICES_CONFIG);
    sdk.subscribeToDeviceConfig(ConfigUtils::DO_NOT_DISTURB_CONFIG);
}