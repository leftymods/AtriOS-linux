#include "platform_info.h"

#include "common.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>

#include <sys/utsname.h>

using namespace AudioSender;

PlatformInfo::PlatformInfo(std::shared_ptr<YandexIO::IDevice> device)
    : device_(std::move(device))
    , firmwareVersion_(device_->softwareVersion())
    , deviceRevision_(device_->platformRevision())
{
    auto commonConfig = device_->configuration()->getServiceConfig("common");
    deviceModel_ = quasar::tryGetString(commonConfig, "deviceTypeForSpeechkit", "");
    if (deviceModel_.empty()) {
        deviceModel_ = quasar::getString(commonConfig, "deviceType");
    }
    deviceManufacturer_ = quasar::getString(commonConfig, "device_manufacturer");
}

std::string PlatformInfo::getAppId() const {
    return "audio_sender";
}

std::string PlatformInfo::getAppName() const {
    return "audio_sender";
}

std::string PlatformInfo::getAppPlatform() const {
#ifdef __ANDROID_API__
    return "Android";
#else
    return "Linux";
#endif
}

std::string PlatformInfo::getAppVersion() const {
    return AUDIOSENDER_VERSION;
}

std::string PlatformInfo::getFirmwareVersion() const {
    return firmwareVersion_;
}

std::string PlatformInfo::getDeviceModel() const {
    return deviceModel_;
}

std::string PlatformInfo::getDeviceRevision() const {
    return deviceRevision_;
}

std::string PlatformInfo::getDeviceManufacturer() const {
    return deviceManufacturer_;
}

std::string PlatformInfo::getOSVersion() const {
    utsname buf{};
    if (uname(&buf) == -1) {
        return "error";
    }

    return buf.release;
}

std::string PlatformInfo::getAppDirectory() const {
    return "";
}

std::string PlatformInfo::getOlsonDbTimezoneName() const {
    return "Europe/Moscow";
}

PlatformInfo::BluetoothInfo PlatformInfo::getBluetoothInfo() const {
    return PlatformInfo::BluetoothInfo();
}

std::vector<std::string> PlatformInfo::getSupportedFeatures() const {
    const auto jsonSupportedFeatures = device_->configuration()->getSupportedFeatures();
    std::vector<std::string> supportedFeatures;
    supportedFeatures.reserve(jsonSupportedFeatures.size());
    for (const auto& el : jsonSupportedFeatures) {
        if (el.isString()) {
            supportedFeatures.push_back(el.asString());
        }
    }
    return supportedFeatures;
}
