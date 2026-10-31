#pragma once

#include "yandex_station2_led_devices.h"
#include "frontal_animation/yandex_station_frontal_controller.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/modules/leds/led_manager/ng/led_devices.h>
#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/sdk_state_observer.h>

class LedDevicesImpl: public LedDevices, public YandexStation2LedDevices, public YandexIO::BackendConfigObserver, public YandexIO::SDKStateObserver {
public:
    LedDevicesImpl(std::string deviceId, std::string defaultAnimationsPath,
                   std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder);
    const std::vector<std::shared_ptr<quasar::LedDevice>>& getDevicesList() const override;
    std::shared_ptr<quasar::LedController> getDefaultDevice() const override;
    std::shared_ptr<YandexStationFrontalController> getLedScreen() override;

    void subscribeToConfig(YandexIO::SDKInterface& sdk);

    void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;
    void onDeviceConfig(const std::string& configName, const std::string& jsonConfigValue) override;

    const std::string& getDefaultAnimationsPath() const;

private:
    void onSDKState(const YandexIO::SDKState& state) override;
    void updateClockState(bool enabled, bool showClockDuringDnd);

private:
    bool isDndMode_{false};
    const std::string deviceId_;
    std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder_;
    std::shared_ptr<quasar::LedController> defaultDevice_;
    std::shared_ptr<YandexStationFrontalController> ledScreen_;
    std::vector<std::shared_ptr<quasar::LedDevice>> devicesList_;
    std::string defaultAnimationsPath_;
};
