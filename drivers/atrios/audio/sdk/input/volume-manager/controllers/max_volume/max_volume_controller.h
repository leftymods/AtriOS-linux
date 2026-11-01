#pragma once

#include <yandex_io/modules/volume_manager/base/volume_manager.h>

#include <yandex_io/interfaces/user_config/i_user_config_provider.h>

#include <memory>

class MaxVolumeController {
public:
    MaxVolumeController(std::weak_ptr<VolumeManager> volumeManager, std::shared_ptr<quasar::IUserConfigProvider> userConfigProvider);
    ~MaxVolumeController();

    void start();

private:
    std::weak_ptr<VolumeManager> volumeManager_;
    std::shared_ptr<quasar::IUserConfigProvider> userConfigProvider_;
    quasar::Lifetime lifetime_;
};
