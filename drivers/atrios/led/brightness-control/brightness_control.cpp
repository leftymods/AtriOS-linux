#include "brightness_control.h"

BrightnessControl::BrightnessControl(std::shared_ptr<ILightSensor> lightSensor, std::weak_ptr<Listener> listener)
    : lightSensor_(std::move(lightSensor))
    , listener_(std::move(listener))
{
}

void BrightnessControl::start() {
    executor_ = std::make_unique<quasar::PeriodicExecutor>([this] { updateBrightness(); }, PERIOD);
}

BrightnessControl::~BrightnessControl() {
    executor_.reset();
}

void BrightnessControl::updateBrightness()
{
    auto lux = lightSensor_->getBrightness();
    ambient_.updateLux(lux);

    if (auto listener = listener_.lock()) {
        listener->onBrightnessChanged(ambient_.getLux());
    }
}
