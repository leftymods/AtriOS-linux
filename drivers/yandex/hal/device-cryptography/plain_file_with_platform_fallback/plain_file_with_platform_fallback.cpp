#include "plain_file_with_platform_fallback.h"

#include <smart_devices/libs/platform_key/platform_key.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/cryptography/cryptography.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

using namespace quasar;

PlainFileWithPlatformFallbackDeviceCryptography::PlainFileWithPlatformFallbackDeviceCryptography(const Json::Value& config) {
    std::string devicePrivateKeyPath = getString(config, "devicePrivateKeyPath");
    std::string devicePublicKeyPath = getString(config, "devicePublicKeyPath");

    if (fileExists(devicePrivateKeyPath) && fileExists(devicePublicKeyPath)) {
        YIO_LOG_INFO("Loading device keys");

        signCryptography_.loadPrivateKey(devicePrivateKeyPath);
        signCryptography_.setPublicKey(getFileContent(devicePublicKeyPath));

        decryptCryptography_.loadPrivateKey(devicePrivateKeyPath);
        decryptPublicCryptoKey_ = getFileContent(devicePublicKeyPath);
        decryptCryptography_.setPublicKey(decryptPublicCryptoKey_);
    } else {
        YIO_LOG_WARN("Cannot find device keys. Using self generated keys");

        decryptCryptography_.loadPrivateKey(getString(config, "privateKeyPath"));
        decryptPublicCryptoKey_ = getFileContent(getString(config, "publicKeyPath"));
        decryptCryptography_.setPublicKey(decryptPublicCryptoKey_);

        signCryptography_.setPrivateKey(getPlatformKey());
    }
}

std::string PlainFileWithPlatformFallbackDeviceCryptography::sign(std::string_view data) {
    return signCryptography_.sign(data);
}

std::string PlainFileWithPlatformFallbackDeviceCryptography::getEncryptPublicKey() const {
    return decryptPublicCryptoKey_;
}

std::string PlainFileWithPlatformFallbackDeviceCryptography::decrypt(std::string_view data) {
    return decryptCryptography_.decrypt(data);
}

std::string PlainFileWithPlatformFallbackDeviceCryptography::getType() const {
    return "plainFileWithPlatformFallback";
}
