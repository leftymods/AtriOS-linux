#pragma once

#include "i_light_sensor.h"

#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class LightSensorHysteresisReader final
    : public ILightSensor {
public:
    LightSensorHysteresisReader(std::shared_ptr<ILightSensor> sensor, std::chrono::milliseconds updatePeriod);

    void start();
    void stop();

    // You should PROHIBIT only BEFORE you do the action which interferes measuring
    // You should ALLOW only AFTER you do the action which stops measuring interfering
    void delayedAllow(std::chrono::milliseconds delay);
    void prohibit();

    float getBrightness() override;

private:
    void readingThread();

private:
    std::mutex mutex_;
    quasar::SteadyConditionVariable cv_;
    std::thread thread_;

    bool stopped_{true};
    // Timestamp from which we can read data. If is nullopt reading is prohibited
    std::optional<std::chrono::steady_clock::time_point> allowedReadFrom_;

    float value_{10.0};

    std::shared_ptr<ILightSensor> lightSensor_;
    std::chrono::milliseconds updatePeriod_;
};
