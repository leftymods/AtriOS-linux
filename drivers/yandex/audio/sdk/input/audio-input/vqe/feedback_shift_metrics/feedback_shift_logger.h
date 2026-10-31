#pragma once

#include <yandex_io/modules/audio_input/vqe/engine/vqe_engine.h>

#include <yandex_io/libs/telemetry/telemetry.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace YandexIO {

    class FeedbackShiftLogger {
    public:
        FeedbackShiftLogger(std::shared_ptr<YandexIO::ITelemetry> telemetry,
                            std::chrono::seconds pingPeriod,
                            std::chrono::seconds throttlePeriod);
        /**
         * @brief: Send "vqeFeedbackShift" metric with payload (if value changed or it's time for "ping" metrica):
         * { "correlation" : int, "feedback_shift" : int, "vqeType" : string }
         * @note: "correlation" and "feedback_shift" can be Json::nullValue
         */
        void log(const YandexIO::VQEEngine& engine, const std::string& vqeType);

    private:
        using Clock = std::chrono::steady_clock;
        std::optional<Clock::time_point> lastSent_;
        std::string vqeType_;
        std::optional<int> shift_;
        std::optional<float> correlation_;
        const std::chrono::seconds pingPeriod_;
        const std::chrono::seconds throttlePeriod_;
        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    };

} /* namespace YandexIO */
