#pragma once

#include "broken_mic_logger.h"

#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <json/json.h>

namespace YandexIO {

    class BrokenMicConfigHandler: public BackendConfigObserver {
    public:
        static const std::string CONFIG_NAME;
        explicit BrokenMicConfigHandler(std::weak_ptr<BrokenMicLogger> brokenMicDetector);
        /**
         * @brief Subscribe to "system_config" via SDKInterface
         */
        void subscribeToConfig(SDKInterface& ioSdk);

        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;

    private:
        Json::Value latestConfig_;
        std::weak_ptr<BrokenMicLogger> brokenMicDetector_;
    };

} /* namespace YandexIO */
