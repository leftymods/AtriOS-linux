#include "vqe_controller_with_yandex_vqe.h"

#include <yandex_io/modules/audio_input/vqe/engine/yandex_c/yandex_vqe_c_engine.h>

namespace YandexIO {

    std::shared_ptr<VqeController> makeVqeControllerWithYandexVqe(std::shared_ptr<YandexIO::ITelemetry> telemetry, std::string deviceType, const VqeController::EnginesList& engines) {
        VqeController::EnginesList allEngines(engines);
        allEngines.emplace_back("yandex", [telemetry{std::move(telemetry)}](const Json::Value& config, const std::string& deviceType) {
            return YandexIO::YandexVQECEngine::create(quasar::AudioReaderConfig{config, deviceType}, telemetry);
        });
        return std::make_shared<VqeController>(std::move(deviceType), allEngines);
    }

} // namespace YandexIO
