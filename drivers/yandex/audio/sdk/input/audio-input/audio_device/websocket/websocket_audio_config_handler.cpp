#include "websocket_audio_config_handler.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/folder/path.h>

#include <cstdio>
#include <fstream>

using namespace quasar;

namespace YandexIO {

    const std::string WebsocketAudioConfigHandler::CONFIG_NAME = "websocket_audio";
    const std::string WebsocketAudioConfigHandler::MARKER_NAME = "force_websocket_audio_device";

    WebsocketAudioConfigHandler::WebsocketAudioConfigHandler(std::string fileMarkerDir, std::string currentAudioDevice)
        : markerFile_{JoinFsPaths(fileMarkerDir, MARKER_NAME)}
        , currentAudioDevice_(std::move(currentAudioDevice))
    {
        YIO_LOG_INFO("Marker file: " << markerFile_);
    }

    Json::Value WebsocketAudioConfigHandler::getMarkerConfig() const {
        if (fileExists(markerFile_)) {
            return tryGetConfigFromFile(markerFile_, Json::Value{});
        }

        return Json::Value{};
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void WebsocketAudioConfigHandler::subscribeToConfig(SDKInterface& ioSdk)
    {
        ioSdk.subscribeToSystemConfig(CONFIG_NAME);
    }

    void WebsocketAudioConfigHandler::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue)
    {
        if (configName != CONFIG_NAME) {
            return;
        }

        const auto optConfig = tryParseJson(jsonConfigValue);
        if (!optConfig.has_value()) {
            YIO_LOG_WARN("Can't parse json config: " << jsonConfigValue);
            return;
        }

        auto& config = *optConfig;
        if (config == latestConfig_) {
            return;
        }

        latestConfig_ = config;

        if (currentAudioDevice_ == "tcp") {
            YIO_LOG_INFO("Websocker audio device ignored due tcp device is active");
            return;
        }

        const bool enabled = tryGetBool(config, "enabled", false);
        bool audioDeviceConfigChanged = false;
        if (enabled) {
            audioDeviceConfigChanged = updateMarker(config);
        } else {
            if (fileExists(markerFile_)) {
                std::remove(markerFile_.data());
                audioDeviceConfigChanged = true;
            }
        }

        if (audioDeviceConfigChanged) {
            YIO_LOG_INFO("Restart to change audio device...");
            ::kill(::getpid(), SIGKILL);
        }
    }

    bool WebsocketAudioConfigHandler::updateMarker(const Json::Value& config) const {
        bool writeFile = true;
        if (fileExists(markerFile_)) {
            const auto markerConfig = tryGetConfigFromFile(markerFile_, Json::Value{});
            writeFile = (markerConfig != config);
        }

        if (writeFile) {
            std::ofstream marker(markerFile_, std::ofstream::trunc);
            marker << quasar::jsonToString(config);
            YIO_LOG_INFO("Marker file " << markerFile_ << " is updated. Config: " + jsonToString(config));
            return true;
        }

        YIO_LOG_DEBUG("No need to update marker file. " << markerFile_);
        return false;
    }

} // namespace YandexIO
