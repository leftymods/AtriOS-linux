#pragma once

#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>

#include <yandex_io/libs/telemetry/telemetry.h>

namespace YandexIO {
    std::shared_ptr<VqeController> makeVqeControllerWithYandexVqe(std::shared_ptr<YandexIO::ITelemetry> telemetry, std::string deviceType, const VqeController::EnginesList& engines = {});
} // namespace YandexIO
