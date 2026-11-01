#pragma once

#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <json/json.h>

#include <string>

namespace YandexIO {

    class WebsocketAudioConfigHandler: public BackendConfigObserver {
    public:
        static const std::string CONFIG_NAME;
        static const std::string MARKER_NAME;

        WebsocketAudioConfigHandler(std::string fileMarkerDir, std::string currentAudioDevice);

        Json::Value getMarkerConfig() const;

        /**
         * @brief Subscribe to "system_config" via YandexIOSDK
         */
        void subscribeToConfig(SDKInterface& ioSdk);

        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;

    private:
        bool updateMarker(const Json::Value& config) const;

        Json::Value latestConfig_;
        std::string markerFile_;
        const std::string currentAudioDevice_;
    };

} /* namespace YandexIO */
