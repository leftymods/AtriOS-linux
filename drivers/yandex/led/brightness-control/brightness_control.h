#pragma once

#include "ambient_light_estimate.h"
#include "i_light_sensor.h"

#include <yandex_io/libs/threading/periodic_executor.h>

#include <memory>

class BrightnessControl {
public:
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void onBrightnessChanged(float sensorBrightness) = 0;
    };
    BrightnessControl(std::shared_ptr<ILightSensor> lightSensor, std::weak_ptr<Listener> listener);
    ~BrightnessControl();

    void start();

private:
    void updateBrightness();

private:
    std::shared_ptr<ILightSensor> lightSensor_;
    AmbientLightEstimate ambient_;

    std::weak_ptr<Listener> listener_;

    static constexpr std::chrono::milliseconds PERIOD = std::chrono::milliseconds(250);
    std::unique_ptr<quasar::PeriodicExecutor> executor_;
};
