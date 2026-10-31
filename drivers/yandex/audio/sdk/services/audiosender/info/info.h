#pragma once

#include <yandex_io/libs/device/i_device.h>

namespace AudioSender {

    enum class ConfigState {
        DISABLED,
        RUNTIME_ENABLED,
        OFFLINE_ENABLED
    };

    ConfigState configState(std::shared_ptr<YandexIO::IDevice> device);

    void switchLaunchCondition(std::shared_ptr<YandexIO::IDevice> device, bool enabled);

} // namespace AudioSender
