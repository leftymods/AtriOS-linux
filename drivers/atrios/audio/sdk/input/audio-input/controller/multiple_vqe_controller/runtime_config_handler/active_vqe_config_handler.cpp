#include "active_vqe_config_handler.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("audio_input_controller");

using namespace quasar;

namespace YandexIO {

    const std::string ActiveVqeConfigHandler::CONFIG_NAME = "vqe";

    ActiveVqeConfigHandler::ActiveVqeConfigHandler(std::weak_ptr<IMultipleVqeController> controller)
        : weakController_(std::move(controller))
    {
    }

    void ActiveVqeConfigHandler::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
        if (configName != CONFIG_NAME) {
            return;
        }
        if (lastConfig_ == jsonConfigValue) {
            return;
        }
        Json::Value config;
        try {
            config = parseJson(jsonConfigValue);
        } catch (const Json::Exception& e) {
            YIO_LOG_ERROR_EVENT("ActiveVqeConfigHandler.ParseConfig", "Can't parse vqe type config: " << e.what());
            return;
        }
        lastConfig_ = jsonConfigValue;

        std::string vqeType;
        if (!config["type"].isNull() && config["type"].isString()) {
            vqeType = config["type"].asString();
        }
        if (auto controller = weakController_.lock()) {
            try {
                YIO_LOG_INFO("Update Active vqe: " << vqeType);
                controller->setActiveVqe(vqeType, config);
            } catch (const std::exception& e) {
                YIO_LOG_ERROR_EVENT("ActiveVqeConfigHandler.FailedSetActiveVqe", "VQE not updated: " << e.what());
            } catch (...) {
                YIO_LOG_ERROR_EVENT("ActiveVqeConfigHandler.FailedSetActiveVqe", "VQE not updated. Uknown exception");
            }
        }
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void ActiveVqeConfigHandler::subscribeToConfig(YandexIO::SDKInterface& sdk) {
        sdk.subscribeToSystemConfig(CONFIG_NAME);
    }

} /* namespace  YandexIO */
