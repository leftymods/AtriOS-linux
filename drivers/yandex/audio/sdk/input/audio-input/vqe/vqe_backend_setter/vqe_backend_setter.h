#pragma once

#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>
#include <yandex_io/modules/audio_input/vqe/engine_factory/vqe_engine_factory.h>

#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <json/json.h>

#include <memory>
#include <string>

namespace YandexIO {

    class VqeBackendSetter: public BackendConfigObserver {
    public:
        VqeBackendSetter(std::weak_ptr<VqeController> vqeController, std::unique_ptr<VqeEngineFactory> factory, const Json::Value& defaultConfig, std::string deviceType);
        /**
         * @brief Subscribe to "system_config" via SDKInterface
         */
        void subscribeToConfig(SDKInterface& sdk);

        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;

    private:
        std::weak_ptr<VqeController> vqeController_;
        std::unique_ptr<VqeEngineFactory> factory_;
        Json::Value latestConfig_;
        const Json::Value defaultConfig_;
        const std::string deviceType_;
    };

} // namespace YandexIO
