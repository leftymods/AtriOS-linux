#pragma once

#include <yandex_io/modules/audio_input/controller/multiple_vqe_controller/i_multiple_vqe_controller.h>

#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <memory>
#include <string>

namespace YandexIO {

    class ActiveVqeConfigHandler: public BackendConfigObserver {
    public:
        explicit ActiveVqeConfigHandler(std::weak_ptr<IMultipleVqeController> controller);
        /**
         * @brief Subscribe to "system_config" via YandexIOSDK
         */
        void subscribeToConfig(YandexIO::SDKInterface& sdk);

        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;

    private:
        static const std::string CONFIG_NAME;
        std::string lastConfig_;
        std::weak_ptr<IMultipleVqeController> weakController_;
    };

} /* namespace YandexIO */
