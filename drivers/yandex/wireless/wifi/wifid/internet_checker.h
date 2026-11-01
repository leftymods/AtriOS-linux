#pragma once

#include <yandex_io/libs/device/device.h>

#include <string>
#include <memory>

namespace quasar {

    class IInternetChecker {
    public:
        struct Result {
            int responseCode{0};
        };

        virtual Result check() = 0;
        virtual ~IInternetChecker() = default;
    };

    std::unique_ptr<IInternetChecker> makeCurlInternetChecker(const std::shared_ptr<YandexIO::IDevice>& device, std::string host, std::string path);
    std::unique_ptr<IInternetChecker> makeGenericInternetChecker(const std::shared_ptr<YandexIO::IDevice>& device, std::string host, std::string path);
} // namespace quasar
