#include "vqe_backend_setter.h"

#include <yandex_io/modules/audio_input/controller/audio_device/utils/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("vqe");

using namespace YandexIO;

namespace {
    const std::string CONFIG_NAME = "vqe";
} // namespace

VqeBackendSetter::VqeBackendSetter(std::weak_ptr<VqeController> vqeController,
                                   std::unique_ptr<VqeEngineFactory> factory,
                                   const Json::Value& defaultConfig,
                                   std::string deviceType)
    : vqeController_(vqeController)
    , factory_(std::move(factory))
    , defaultConfig_(defaultConfig)
    , deviceType_(std::move(deviceType))
{
}

void VqeBackendSetter::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName != CONFIG_NAME) {
        return;
    }
    Json::Value config;
    try {
        config = quasar::parseJson(jsonConfigValue);
    } catch (const Json::Exception& e) {
        YIO_LOG_ERROR_EVENT("VqeBackendSetter.InvalidBackendConfigJson", "Can't parse vqe type config: " << e.what());
        return;
    }
    if (latestConfig_ == config) {
        return;
    }
    latestConfig_ = config;
    Json::Value mergedConfig(defaultConfig_);
    mergeVqeConfigsWithJsonPreset(config, mergedConfig);

    if (auto svqe = vqeController_.lock()) {
        std::shared_ptr<YandexIO::VQEEngine> engine;
        try {
            engine = factory_->createEngine(mergedConfig, deviceType_);
        } catch (const std::runtime_error& e) {
            YIO_LOG_ERROR_EVENT("VqeBackendSetter.CreateEngine.Exception", "Can't create VQE engine with config: " << mergedConfig << " with exception " << e.what());
            return;
        }
        quasar::AudioReaderConfig arConfig(mergedConfig, deviceType_);
        svqe->setEngine(engine, arConfig.vqeTypeName, arConfig.preset);
        YIO_LOG_INFO("New VQE engine is set with config: " << mergedConfig);
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void VqeBackendSetter::subscribeToConfig(SDKInterface& sdk) {
    sdk.subscribeToSystemConfig(CONFIG_NAME);
}
