#include "yandex_station2_hal.h"

#include <smart_devices/libs/iot/tuya_smart_link/iot_discovery_provider_impl.h>

#include <yandex_io/libs/bluetooth/bluetooth_capabilities_impl.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <stdexcept>

using namespace quasar;

std::optional<YandexIO::HALInfo> YandexStation2HAL::getHALInfo() {
    return std::nullopt;
}

std::shared_ptr<BluetoothLE> YandexStation2HAL::createBluetoothLE() {
    return nullptr;
}

BluetoothCapabilities& YandexStation2HAL::getBluetoothCapabilities() {
    static BluetoothCapabilitiesImpl singleton(/*streamIn*/ false, /*streamOut*/ false, /*ble*/ true);
    return singleton;
}

std::unique_ptr<YandexIO::DeviceCryptography> YandexStation2HAL::createDeviceCryptography(const Json::Value& config) {
    const auto cryptographyType = getString(config, "cryptographyType");
    if (cryptographyType == "plainFile") {
        YIO_LOG_INFO("Using plain file device key");
        return createPlainFileDeviceCryptography(config);
    } else if (cryptographyType == "keymasterProxy") {
        YIO_LOG_INFO("Using keymaster proxy device key");
        return createKeymasterProxyCryptography(config);
    } else {
        throw std::invalid_argument("Unknown cryptography used in quasar.cfg: '" + cryptographyType + "', expected one of: 'plainFile' or 'keymasterProxy'.");
    }
}

std::unique_ptr<YandexIO::PlainFileDeviceCryptography> YandexStation2HAL::createPlainFileDeviceCryptography(const Json::Value& config) {
    Cryptography::KeyPair keyPair;
    Cryptography::Padding rsaPadding = Cryptography::parsePadding(std::stoi(getString(config, "plainFileRsaPadding")));
    try {
        keyPair = Cryptography::KeyPair::fromPrivateKeyFile(getString(config, "devicePrivateKeyPath"));
    } catch (const std::runtime_error& e) {
        YIO_LOG_WARN("Cannot load device private key: " << e.what() << ", using fallback key");
        keyPair = Cryptography::KeyPair::fromPrivateKeyFile(getString(config, "privateKeyPath"));
    }
    return std::make_unique<YandexIO::PlainFileDeviceCryptography>(keyPair, rsaPadding);
}

std::unique_ptr<quasar::KeymasterProxyCryptography> YandexStation2HAL::createKeymasterProxyCryptography(const Json::Value& config) {
    return std::make_unique<KeymasterProxyCryptography>(config);
}

std::shared_ptr<YandexIO::IIotDiscoveryProvider> YandexStation2HAL::createIotDiscoveryProvider() {
    return std::make_shared<IotDiscoveryProviderImpl>();
}
