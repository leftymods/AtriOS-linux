#include "max_volume_controller.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

using namespace quasar;

MaxVolumeController::MaxVolumeController(std::weak_ptr<VolumeManager> volumeManager, std::shared_ptr<IUserConfigProvider> userConfigProvider)
    : volumeManager_(volumeManager)
    , userConfigProvider_(userConfigProvider)
{
}

MaxVolumeController::~MaxVolumeController() {
    lifetime_.die();
}

void MaxVolumeController::start() {
    userConfigProvider_->jsonChangedSignal(IUserConfigProvider::ConfigScope::AUXILIARY, "alice4business/max_volume").connect([this](const auto& jsonPtr) {
        YIO_LOG_INFO("Received max volume config: " << (jsonPtr ? quasar::jsonToString(*jsonPtr) : "null"));

        auto smanager = volumeManager_.lock();
        if (!smanager) {
            return;
        }

        if (jsonPtr && jsonPtr->isInt()) {
            smanager->setAliceVolumeMaxLimit(jsonPtr->asInt());
        } else {
            smanager->setAliceVolumeMaxLimit(std::nullopt);
        }
    }, lifetime_);
}
