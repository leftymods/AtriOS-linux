#include "led_screen_stats_reporter.h"

#include <yandex_io/libs/json_utils/json_utils.h>

LedScreenStatsReporter::LedScreenStatsReporter(
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
    std::shared_ptr<YandexIO::SDKInterface> sdk,
    const Json::Value& config)
    : sdk_(std::move(sdk))
    , yandexStationFrontalController_(std::move(yandexStationFrontalController))
{
    const auto sendLastGifPeriodMs = std::chrono::milliseconds(config["sendLedScreenStatsPeriodMs"].asUInt64());
    sendMetricaExecutor_ = std::make_unique<quasar::PeriodicExecutor>(
        [this] { sendLedScreenStats(); },
        sendLastGifPeriodMs);
}

void LedScreenStatsReporter::sendLedScreenStats() {
    auto lastAnimationName = yandexStationFrontalController_->getLastAnimationName();
    sdk_->sendCategoricalMetrics("lastAnimationName", lastAnimationName);

    auto brightness = yandexStationFrontalController_->getBrightness();
    sdk_->sendNumericMetrics("ledControllerBrightness", brightness);

    auto droppedFrames = yandexStationFrontalController_->getDroppedFrames();
    sdk_->sendNumericMetrics("ledDroppedFrames", droppedFrames);
}
