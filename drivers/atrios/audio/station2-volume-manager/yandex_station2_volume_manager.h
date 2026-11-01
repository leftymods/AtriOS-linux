#pragma once

#include "smart_devices/platforms/yandexstation_2/platformd/leds/volume/rcu_volume_conductor.h"
#include "smart_devices/platforms/yandexstation_2/platformd/leds/volume/volume_conductor.h"

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/volume_manager/base/volume_manager.h>
#include <yandex_io/sdk/backend_config_observer.h>

#include <mutex>

class YandexStation2VolumeManager: public VolumeManager, public YandexIO::BackendConfigObserver {
public:
    YandexStation2VolumeManager(std::shared_ptr<YandexIO::IDevice> device,
                                const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
                                std::shared_ptr<YandexIO::SDKInterface> sdk,
                                const std::string& currentVolumeFilename, const std::string& currentMuteStateFilename,
                                std::shared_ptr<quasar::LedManager> ledManager,
                                std::shared_ptr<VolumeConductor> volumeConductor,
                                std::shared_ptr<RcuVolumeConductor> rcuVolumeConductor,
                                const Json::Value& config);

    void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;
    void subscribeToSystemConfig();

    int minVolume() override;
    int maxVolume() override;

protected:
    int volumeStep() override;
    int scaleFromAlice(int aliceVolume) override;
    int scaleToAlice(int platformVolume) override;
    int initialVolume() override;
    void setVolumeImplementation(int platformVolume) override;
    void playVolumeLed(int platformValue, bool muted, const std::string& source) override;
    void onChangeVolumeAVRCP(int volumePercent) override;
    int alarmRaisingPlatformVolumeStep() const override;

private:
    static constexpr int RING_TICKS = 105;
    static constexpr float TICKS_PER_ALICE_VOLUME_STEP = (float)RING_TICKS / MAX_ALICE_VOLUME;

    struct VolumeRange {
        int min;
        int max;
    };

    static constexpr VolumeRange DEFAULT_ANDROID_VOLUME{.min = 2, .max = 100};

    // Initial values from quasar.cfg
    VolumeRange cfgAndroidVolume_ = DEFAULT_ANDROID_VOLUME;

    // These values come from system_config or from cfgAndroidVolumeRange_ if config is empty
    VolumeRange androidVolume_ = DEFAULT_ANDROID_VOLUME;

    std::mutex lastVolumeMutex_;
    int lastAndroidVolume_{0};
    int lastAliceVolume_{0};
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<quasar::ipc::IConnector> toVolumeSetd_;

    void handleConfigUpdate(const Json::Value& config);
    void sendLastVolume();
    int convertToAndroidVolume(int platformVolume) const;

    std::shared_ptr<VolumeConductor> volumeConductor_;
    std::shared_ptr<RcuVolumeConductor> rcuVolumeConductor_;
};
