#include "broken_mic_config_handler.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("broken_mic");

using namespace quasar;

namespace YandexIO {

    const std::string BrokenMicConfigHandler::CONFIG_NAME = "brokenMicDetector";

    BrokenMicConfigHandler::BrokenMicConfigHandler(std::weak_ptr<BrokenMicLogger> brokenMicDetector)
        : brokenMicDetector_(std::move(brokenMicDetector))
    {
    }

    void BrokenMicConfigHandler::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
        if (configName != CONFIG_NAME) {
            return;
        }
        Json::Value config;
        try {
            config = parseJson(jsonConfigValue);
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("BrokenMicConfigHandler.InvalidJson", "Can't parse broken mic detector config: " << e.what());
            return;
        }
        if (config == latestConfig_) {
            return;
        }
        latestConfig_ = config;
        if (auto sdetector = brokenMicDetector_.lock()) {
            YIO_LOG_INFO("Update BrokenMicDetector config: " << jsonConfigValue);
            sdetector->updateConfig(latestConfig_);
        }
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void BrokenMicConfigHandler::subscribeToConfig(SDKInterface& ioSdk) {
        ioSdk.subscribeToSystemConfig(CONFIG_NAME);
    }

} /* namespace  YandexIO */
