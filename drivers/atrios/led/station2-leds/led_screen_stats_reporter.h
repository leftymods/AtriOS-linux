#pragma once

#include "frontal_animation/yandex_station_frontal_controller.h"

#include <yandex_io/libs/threading/periodic_executor.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <memory>

class LedScreenStatsReporter {
public:
    LedScreenStatsReporter(std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController,
                           std::shared_ptr<YandexIO::SDKInterface> sdk,
                           const Json::Value& config);

    void sendLedScreenStats();

private:
    const std::shared_ptr<YandexIO::SDKInterface> sdk_;
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController_;
    std::unique_ptr<quasar::PeriodicExecutor> sendMetricaExecutor_;
};
