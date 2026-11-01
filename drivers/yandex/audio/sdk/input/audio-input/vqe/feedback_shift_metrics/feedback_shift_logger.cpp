#include "feedback_shift_logger.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

namespace YandexIO {

    FeedbackShiftLogger::FeedbackShiftLogger(std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                             std::chrono::seconds pingPeriod,
                                             std::chrono::seconds throttlePeriod)
        : pingPeriod_(pingPeriod)
        , throttlePeriod_(throttlePeriod)
        , telemetry_(std::move(telemetry))
    {
        if (throttlePeriod >= pingPeriod && throttlePeriod != std::chrono::seconds(0)) {
            std::stringstream ss;
            ss << "Throttle Period(" << throttlePeriod_.count() << "sec) should not be greater than Ping Period(" << pingPeriod_.count() << "sec)";
            throw std::runtime_error(ss.str());
        }
    }

    void FeedbackShiftLogger::log(const YandexIO::VQEEngine& engine, const std::string& vqeType) {
        auto timePassed = [](const auto& lastTs, const auto& pingPeriod) {
            if (!lastTs.has_value()) {
                return true;
            }
            const auto now = Clock::now();
            const auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - *lastTs);
            return diff >= pingPeriod;
        };
        const bool shouldThrottle = !timePassed(lastSent_, throttlePeriod_);
        if (shouldThrottle) {
            return;
        }

        const auto shift = engine.getFeedbackShift();
        const auto correlation = engine.getFeedbackShiftCorrelation();
        bool changed = false;
        if (vqeType_ != vqeType) {
            vqeType_ = vqeType;
            changed = true;
        }

        if (shift_ != shift) {
            shift_ = shift;
            changed = true;
        }

        if (correlation_ != correlation) {
            correlation_ = correlation;
            changed = true;
        }

        const bool shouldPing = timePassed(lastSent_, pingPeriod_);
        if (changed || shouldPing) {
            lastSent_ = Clock::now();
            Json::Value payload;
            payload["vqeType"] = vqeType_;
            payload["feedback_shift"] = Json::nullValue;
            if (shift_.has_value()) {
                payload["feedback_shift"] = *shift_;
            }
            payload["correlation"] = Json::nullValue;
            if (correlation_.has_value()) {
                payload["correlation"] = *correlation_;
            }
            const auto jsonStr = quasar::jsonToString(payload);
            YIO_LOG_INFO("VqeFeedbackShift: " << jsonStr);
            telemetry_->reportEvent("vqeFeedbackShift", jsonStr);
        }
    }

} /* namespace YandexIO */
