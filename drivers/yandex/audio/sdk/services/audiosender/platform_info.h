#pragma once

#include <yandex_io/libs/device/device.h>

#include <speechkit/PlatformInfo.h>

namespace AudioSender {

    class PlatformInfo: public ::SpeechKit::PlatformInfo {
    public:
        PlatformInfo(std::shared_ptr<YandexIO::IDevice> device);

        std::string getAppId() const override;
        std::string getAppName() const override;
        std::string getAppPlatform() const override;
        std::string getAppVersion() const override;
        std::string getFirmwareVersion() const override;
        std::string getDeviceModel() const override;
        std::string getDeviceRevision() const override;
        std::string getDeviceManufacturer() const override;
        std::vector<std::string> getSupportedFeatures() const override;
        std::string getOSVersion() const override;
        std::string getAppDirectory() const override;
        std::string getOlsonDbTimezoneName() const override;
        BluetoothInfo getBluetoothInfo() const override;

    private:
        std::shared_ptr<YandexIO::IDevice> device_;

        std::string firmwareVersion_;
        std::string deviceModel_;
        std::string deviceRevision_;
        std::string deviceManufacturer_;
    };

} // namespace AudioSender
