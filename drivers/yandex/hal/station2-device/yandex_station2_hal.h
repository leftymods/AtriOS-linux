#pragma once

#include <yandex_io/libs/device_cryptography/keymaster_proxy/keymaster_proxy_device_cryptography.h>
#include <yandex_io/libs/device_cryptography/plain_file/plain_file_device_cryptography.h>
#include <yandex_io/libs/hal/hal.h>

class YandexStation2HAL: public YandexIO::HAL {
    std::optional<YandexIO::HALInfo> getHALInfo() override;

    std::shared_ptr<BluetoothLE> createBluetoothLE() override;

    std::unique_ptr<YandexIO::DeviceCryptography> createDeviceCryptography(const Json::Value& config) override;

    quasar::BluetoothCapabilities& getBluetoothCapabilities() override;

    std::shared_ptr<YandexIO::IIotDiscoveryProvider> createIotDiscoveryProvider() override;

private:
    static std::unique_ptr<YandexIO::PlainFileDeviceCryptography> createPlainFileDeviceCryptography(const Json::Value& config);

    static std::unique_ptr<quasar::KeymasterProxyCryptography> createKeymasterProxyCryptography(const Json::Value& config);
};
