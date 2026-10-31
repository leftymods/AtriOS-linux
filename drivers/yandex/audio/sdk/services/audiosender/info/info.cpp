#include "info.h"

#include <yandex_io/libs/configuration/configuration.h>
#include <yandex_io/libs/json_utils/json_utils.h>

#include <util/folder/path.h>

AudioSender::ConfigState AudioSender::configState(std::shared_ptr<YandexIO::IDevice> device) {
    const auto& yiodConfig = device->configuration()->getServiceConfig("yiod");

    if (quasar::tryGetBool(yiodConfig, "audiosenderMode", false)) {
        return AudioSender::ConfigState::OFFLINE_ENABLED;
    }

    auto runtimeMarkerPath = quasar::tryGetString(yiodConfig, "audiosenderRuntimeMarkerPath");
    if (TFsPath(runtimeMarkerPath).Exists()) {
        return AudioSender::ConfigState::RUNTIME_ENABLED;
    }

    return AudioSender::ConfigState::DISABLED;
}

void AudioSender::switchLaunchCondition(std::shared_ptr<YandexIO::IDevice> device, bool enabled) {
    const auto& yiodConfig = device->configuration()->getServiceConfig("yiod");
    auto runtimeMarkerPath = quasar::tryGetString(yiodConfig, "audiosenderRuntimeMarkerPath");

    const TFsPath flag(runtimeMarkerPath);
    if (enabled) {
        flag.Parent().MkDirs();
        flag.Touch();
    } else {
        flag.ForceDelete();
    }
}
