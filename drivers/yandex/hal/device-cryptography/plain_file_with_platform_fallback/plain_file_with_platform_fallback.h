#pragma once

#include <json/json.h>

#include <yandex_io/libs/cryptography/cryptography.h>
#include <yandex_io/libs/device_cryptography/device_cryptography.h>

namespace quasar {
    /**
     * @brief legacy impl for cryptogaphy.
     *
     * There are two modes of operation with fallback to the latter:
     *
     * 1. attempts to load key pair from paths given in `config` in `devicePrivateKeyPath` / `devicePublicKeyPath` and use them for both encrypt / decrypt ops.
     *    this represents "unique per-device keys".
     * 2. if those files are missing falls back to:
     *      a. looks up `privateKeyPath` / `publicKeyPath` in `config` and use keypair from these files ONLY FOR `decrypt` ! These keys are assumed self-generated.
     *      b. uses `platformKey.h` to extract packaged platform key to use it for ONLY FOR `encrypt` operation.
     *    this represents "per-platform keys".
     *
     * Mode #2 is also used for development purposes for devices where #1 requires manufacturing effort (e.g. chaber-extracted keys).
     */
    class PlainFileWithPlatformFallbackDeviceCryptography: public YandexIO::DeviceCryptography {
        Cryptography signCryptography_;    // previously known as backendCryptography_
        Cryptography decryptCryptography_; // previously known as wifiCryptography_
        std::string decryptPublicCryptoKey_;

    public:
        explicit PlainFileWithPlatformFallbackDeviceCryptography(const Json::Value& config);

        std::string sign(std::string_view data) override;
        std::string getEncryptPublicKey() const override;
        std::string decrypt(std::string_view data) override;

        std::string getType() const override;
    };
} // namespace quasar
