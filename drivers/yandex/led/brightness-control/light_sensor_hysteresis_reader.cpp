#include "light_sensor_hysteresis_reader.h"

LightSensorHysteresisReader::LightSensorHysteresisReader(std::shared_ptr<ILightSensor> sensor, std::chrono::milliseconds updatePeriod)
    : lightSensor_(std::move(sensor))
    , updatePeriod_(updatePeriod)
{
}

void LightSensorHysteresisReader::start() {
    std::scoped_lock guard(mutex_);

    if (stopped_) {
        stopped_ = false;
        thread_ = std::thread(&LightSensorHysteresisReader::readingThread, this);
    }
}

void LightSensorHysteresisReader::stop() {
    {
        std::scoped_lock guard(mutex_);
        stopped_ = true;
    }

    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void LightSensorHysteresisReader::delayedAllow(std::chrono::milliseconds delay) {
    std::scoped_lock guard(mutex_);

    if (!allowedReadFrom_) {
        allowedReadFrom_ = std::chrono::steady_clock::now() + delay;
        cv_.notify_all();
    }
}

void LightSensorHysteresisReader::prohibit() {
    std::scoped_lock guard(mutex_);

    allowedReadFrom_.reset();
    cv_.notify_all();
}

float LightSensorHysteresisReader::getBrightness() {
    std::scoped_lock guard(mutex_);

    return value_;
}

void LightSensorHysteresisReader::readingThread() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!stopped_) {
        if (allowedReadFrom_) {
            if (std::chrono::steady_clock::now() >= *allowedReadFrom_) {
                value_ = lightSensor_->getBrightness();

                cv_.wait_for(lock, updatePeriod_, [this]() {
                    return !allowedReadFrom_ || stopped_;
                });
            } else {
                cv_.wait_until(lock, *allowedReadFrom_, [this]() {
                    return !allowedReadFrom_ || stopped_;
                });
            }
        } else {
            cv_.wait(lock, [this]() {
                return allowedReadFrom_ || stopped_;
            });
        }
    }
}
